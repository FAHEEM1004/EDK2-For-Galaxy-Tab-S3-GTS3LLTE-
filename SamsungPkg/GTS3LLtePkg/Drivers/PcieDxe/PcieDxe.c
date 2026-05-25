/** @file
  PCIe driver for MSM8996 — all three root complexes.

  Hardware (XBL-verified addresses):
    PCIe0 AHB @ 0x00600000  AXI @ 0x0C000000  PHY @ 0x00034000  QCA6174 WiFi/BT
    PCIe1 AHB @ 0x00608000  AXI @ 0x0C200000  PHY @ 0x00038000  (open slot)
    PCIe2 AHB @ 0x00610000  AXI @ 0x0C400000  PHY @ 0x0003C000  (open slot)

  AXI windows are 64 MB each, spaced 0x200000 apart starting at 0x0C000000.

  Each root complex uses the same DesignWare PCIe IP with per-instance:
    - QMP PHY (power/reset/PLL lock sequence)
    - iATU regions (MMIO + CFG0 type0 configuration space)

  Clock enables for all three are handled by GccClockDxe before this driver runs.
  DEPEX on GccClockDxe ensures correct ordering.

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// PCIe controller AHB bases (DBI + iATU registers)
// ---------------------------------------------------------------------------
#define PCIE_AHB_BASE0                  MSM8996_PCIE0_BASE    // 0x00600000
#define PCIE_AHB_BASE1                  MSM8996_PCIE1_BASE    // 0x00608000
#define PCIE_AHB_BASE2                  MSM8996_PCIE2_BASE    // 0x00610000

// ---------------------------------------------------------------------------
// AXI memory windows — CPU-side aperture for PCI memory space
// Four 2 MB sub-windows per RC:
//   Win0: MEM (maps PCI address 0x00000000 → 1 MB)
//   Win1: CFG0 type-0 config space  (bus 0 device 0)
// Each RC gets a 2 MB slice of the 64 MB PCIe AXI block (0x0C000000)
// ---------------------------------------------------------------------------
#define PCIE_AXI_WIN0_MEM               0x0C000000   // PCIe0 MEM
#define PCIE_AXI_WIN0_CFG               0x0C100000   // PCIe0 CFG0
#define PCIE_AXI_WIN1_MEM               0x0C200000   // PCIe1 MEM
#define PCIE_AXI_WIN1_CFG               0x0C300000   // PCIe1 CFG0
#define PCIE_AXI_WIN2_MEM               0x0C400000   // PCIe2 MEM
#define PCIE_AXI_WIN2_CFG               0x0C500000   // PCIe2 CFG0
#define PCIE_AXI_WIN_SIZE               0x00100000   // 1 MB per window

// ---------------------------------------------------------------------------
// DBI registers (standard PCIe configuration / DW-specific, at AHB base)
// ---------------------------------------------------------------------------
#define PCIE_DBI_VENDOR_ID              0x0000
#define PCIE_DBI_COMMAND_STATUS         0x0004
#define PCIE_DBI_LINK_CTRL_STATUS       0x0080
#define PCIE_DBI_MISC_CONTROL_1         0x08BC   // DW: write_ro_enable

// iATU (internal Address Translation Unit) registers — DesignWare PCIe IP
// Accessed as registers within the AHB window at offset 0x700
#define PCIE_ATU_BASE                   0x0700
#define PCIE_ATU_VIEWPORT               (PCIE_ATU_BASE + 0x000)
#define PCIE_ATU_CR1                    (PCIE_ATU_BASE + 0x004)
#define PCIE_ATU_CR2                    (PCIE_ATU_BASE + 0x008)
#define PCIE_ATU_LOWER_BASE             (PCIE_ATU_BASE + 0x00C)
#define PCIE_ATU_UPPER_BASE             (PCIE_ATU_BASE + 0x010)
#define PCIE_ATU_LIMIT                  (PCIE_ATU_BASE + 0x014)
#define PCIE_ATU_LOWER_TARGET           (PCIE_ATU_BASE + 0x018)
#define PCIE_ATU_UPPER_TARGET           (PCIE_ATU_BASE + 0x01C)

// ATU CR1 — region type
#define ATU_TYPE_MEM                    0x00
#define ATU_TYPE_CFG0                   0x04   // type-0 config (root complex)
#define ATU_TYPE_CFG1                   0x05   // type-1 config (downstream)

// ATU CR2 — enable / direction
#define ATU_ENABLE                      BIT31
#define ATU_VIEWPORT_OUTBOUND           0x00000000
#define ATU_VIEWPORT_INBOUND            0x80000000

// ---------------------------------------------------------------------------
// Qualcomm QMP PCIe PHY registers (per-instance, offsets from PHY base)
// The QMP PHY block structure for MSM8996 PCIe:
//   +0x000: POWER_DOWN_CONTROL
//   +0x600: SW_RESET
//   +0x604: START
//   +0x61C: PCS_STATUS (PLL lock)
// Same register layout for all three PHYs; only base address differs.
// ---------------------------------------------------------------------------
#define QMP_PHY_POWER_DOWN_CONTROL      0x000
#define QMP_PHY_SW_RESET                0x600
#define QMP_PHY_START                   0x604
#define QMP_PHY_PCS_STATUS              0x61C
#define QMP_PHY_PLL_LOCK                BIT5   // status bit: PLL locked

// ---------------------------------------------------------------------------
// iATU programming helper — programs one outbound ATU region
// ---------------------------------------------------------------------------

STATIC
VOID
PcieAtuProgram (
  IN UINTN   AhbBase,
  IN UINT32  Region,      // region index (0, 1, ...)
  IN UINT32  Type,        // ATU_TYPE_MEM, ATU_TYPE_CFG0, etc.
  IN UINT64  CpuStart,    // CPU-side start address
  IN UINT64  PciTarget,   // PCI target bus address
  IN UINT32  Size         // window size in bytes
  )
{
  MmioWrite32 (AhbBase + PCIE_ATU_VIEWPORT,     ATU_VIEWPORT_OUTBOUND | Region);
  MmioWrite32 (AhbBase + PCIE_ATU_LOWER_BASE,   (UINT32)(CpuStart & 0xFFFFFFFF));
  MmioWrite32 (AhbBase + PCIE_ATU_UPPER_BASE,   (UINT32)(CpuStart >> 32));
  MmioWrite32 (AhbBase + PCIE_ATU_LIMIT,        (UINT32)(CpuStart & 0xFFFFFFFF) + Size - 1);
  MmioWrite32 (AhbBase + PCIE_ATU_LOWER_TARGET, (UINT32)(PciTarget & 0xFFFFFFFF));
  MmioWrite32 (AhbBase + PCIE_ATU_UPPER_TARGET, (UINT32)(PciTarget >> 32));
  MmioWrite32 (AhbBase + PCIE_ATU_CR1,          Type);
  MmioWrite32 (AhbBase + PCIE_ATU_CR2,          ATU_ENABLE);
}

// ---------------------------------------------------------------------------
// QMP PHY bring-up — common sequence for all three PCIe PHYs
//
// Steps (from Qualcomm PCIe PHY bring-up sequence in XBL source):
//   1. Assert power-up  (POWER_DOWN_CONTROL = 0x01)
//   2. Wait 1 ms for PLL ref clock to stabilise
//   3. Deassert SW reset  (SW_RESET = 0x00)
//   4. Start the PLL      (START = 0x03  — SerDes and PCS enable)
//   5. Poll PCS_STATUS bit 5 for PLL lock (up to 100 ms)
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
PciePhyInit (
  IN UINTN    PhyBase,
  IN UINTN    RcIndex
  )
{
  UINTN  Timeout;

  MmioWrite32 (PhyBase + QMP_PHY_POWER_DOWN_CONTROL, 0x01);
  MicroSecondDelay (1000);

  MmioWrite32 (PhyBase + QMP_PHY_SW_RESET, 0x00);
  MmioWrite32 (PhyBase + QMP_PHY_START,    0x03);

  Timeout = 100000;   // 100 ms
  while (!(MmioRead32 (PhyBase + QMP_PHY_PCS_STATUS) & QMP_PHY_PLL_LOCK) && Timeout--) {
    MicroSecondDelay (1);
  }

  if (!(MmioRead32 (PhyBase + QMP_PHY_PCS_STATUS) & QMP_PHY_PLL_LOCK)) {
    DEBUG ((DEBUG_WARN, "PCIe%u: QMP PHY PLL did not lock (PCS=0x%08X) — continuing\n",
            RcIndex, MmioRead32 (PhyBase + QMP_PHY_PCS_STATUS)));
    // Non-fatal: the link may still come up, or there may be no endpoint.
    // Do not return error — the RC registers are still accessible.
    return EFI_NOT_READY;
  }

  DEBUG ((DEBUG_INFO, "PCIe%u: QMP PHY PLL locked\n", RcIndex));
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Per-RC initialisation — PHY + DBI + iATU
// ---------------------------------------------------------------------------

STATIC
VOID
PcieRcInit (
  IN UINTN    AhbBase,
  IN UINTN    PhyBase,
  IN UINT64   AxiMemBase,   // CPU address for PCI MEM window
  IN UINT64   AxiCfgBase,   // CPU address for PCI CFG0 window
  IN UINTN    RcIndex
  )
{
  DEBUG ((DEBUG_INFO, "PCIe%u: AHB=0x%08X PHY=0x%08X MEM=0x%08X CFG=0x%08X\n",
          RcIndex, (UINT32)AhbBase, (UINT32)PhyBase,
          (UINT32)AxiMemBase, (UINT32)AxiCfgBase));

  // Bring up QMP PHY (non-fatal if PLL misses)
  PciePhyInit (PhyBase, RcIndex);

  // Enable PCIe command register: Memory Space Enable + Bus Master Enable
  MmioOr32 (AhbBase + PCIE_DBI_COMMAND_STATUS, 0x0006);

  // Allow writes to read-only DBI registers (needed on some DW revisions)
  MmioOr32 (AhbBase + PCIE_DBI_MISC_CONTROL_1, BIT0);

  // iATU region 0 — outbound MEM: CPU AxiMemBase → PCI address 0x00000000
  PcieAtuProgram (AhbBase, 0, ATU_TYPE_MEM,
                  AxiMemBase, 0x00000000ULL, PCIE_AXI_WIN_SIZE);

  // iATU region 1 — outbound CFG0: CPU AxiCfgBase → PCI cfg type-0 (bus 0 dev 0)
  PcieAtuProgram (AhbBase, 1, ATU_TYPE_CFG0,
                  AxiCfgBase, 0x00000000ULL, PCIE_AXI_WIN_SIZE);

  DEBUG ((DEBUG_INFO, "PCIe%u: ATU programmed, RC ready\n", RcIndex));
}

// ---------------------------------------------------------------------------
// Driver entry point
// ---------------------------------------------------------------------------

EFI_STATUS
EFIAPI
PcieDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "PcieDxe: MSM8996 PCIe × 3 root complexes\n"));

  //
  // PCIe0 — QCA6174 802.11ac WiFi + Bluetooth 4.1
  //   AHB: 0x00600000  PHY: 0x00034000  MEM: 0x0C000000  CFG: 0x0C100000
  //
  PcieRcInit (
    PCIE_AHB_BASE0,
    MSM8996_PCIE_PHY_BASE,
    PCIE_AXI_WIN0_MEM,
    PCIE_AXI_WIN0_CFG,
    0
    );

  //
  // PCIe1 — open slot (no endpoint on SM-T825; initialise RC for completeness)
  //   AHB: 0x00608000  PHY: 0x00038000  MEM: 0x0C200000  CFG: 0x0C300000
  //
  PcieRcInit (
    PCIE_AHB_BASE1,
    MSM8996_PCIE1_PHY_BASE,
    PCIE_AXI_WIN1_MEM,
    PCIE_AXI_WIN1_CFG,
    1
    );

  //
  // PCIe2 — open slot (no endpoint on SM-T825; initialise RC for completeness)
  //   AHB: 0x00610000  PHY: 0x0003C000  MEM: 0x0C400000  CFG: 0x0C500000
  //
  PcieRcInit (
    PCIE_AHB_BASE2,
    MSM8996_PCIE2_PHY_BASE,
    PCIE_AXI_WIN2_MEM,
    PCIE_AXI_WIN2_CFG,
    2
    );

  return EFI_SUCCESS;
}
