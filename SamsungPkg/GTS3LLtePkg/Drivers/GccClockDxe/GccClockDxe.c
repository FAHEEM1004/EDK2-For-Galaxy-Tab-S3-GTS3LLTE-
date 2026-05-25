/** @file
  GCC (Global Clock Controller) driver for MSM8996.

  Manages clock enable/disable and rate-setting for all peripherals.
  Base address: 0x00300000 (640 KB, from XBL MMIO table).

  Key clock domains implemented:
    - BLSP1/BLSP2 AHB + QUP clocks (UART, I2C, SPI)
    - USB3: iface, master, mock_utmi, pipe, phy_aux, sleep, aggre2_usb3_axi
    - UFS: axi, ahb, tx_cfg, rx_cfg, ice_core
    - Display: mdss_ahb, mdss_axi, mdss_mdp, dsi esc
    - CRYPTO: ce1_clk, ce1_ahb_clk
    - PCIE: pcie0/1/2 aux, pipe, bus_master, bus_slave, cfg_ahb

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ClockProtocol.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// GCC register offsets (MSM8996 GCC, base 0x00300000)
// ---------------------------------------------------------------------------

// BLSP1
#define GCC_BLSP1_AHB_CBCR              0x17004
#define GCC_BLSP1_UART2_APPS_CMD_RCGR   0x1C000
#define GCC_BLSP1_UART2_APPS_CFG_RCGR   0x1C004
#define GCC_BLSP1_UART2_APPS_M          0x1C008
#define GCC_BLSP1_UART2_APPS_N          0x1C00C
#define GCC_BLSP1_UART2_APPS_D          0x1C010
#define GCC_BLSP1_UART2_APPS_CBCR       0x1C014

// BLSP2 (contains debug UART @ 0x075B0000 = BLSP2 UART1)
#define GCC_BLSP2_AHB_CBCR              0x25004
#define GCC_BLSP2_UART1_APPS_CMD_RCGR   0x27000
#define GCC_BLSP2_UART1_APPS_CFG_RCGR   0x27004
#define GCC_BLSP2_UART1_APPS_M          0x27008
#define GCC_BLSP2_UART1_APPS_N          0x2700C
#define GCC_BLSP2_UART1_APPS_D          0x27010
#define GCC_BLSP2_UART1_APPS_CBCR       0x27014
// BLSP2 QUP I2C for SM5705 charger
#define GCC_BLSP2_QUP2_I2C_APPS_CBCR   0x26008

// USB 3.0
#define GCC_USB_HS_SYSTEM_CBCR          0x41004
#define GCC_USB30_MASTER_CBCR           0xF008
#define GCC_USB30_SLEEP_CBCR            0xF00C
#define GCC_USB30_MOCK_UTMI_CBCR        0xF010
#define GCC_USB30_PHY_AUX_CBCR          0x50000
#define GCC_USB30_PIPE_CBCR             0x50004
#define GCC_USB_PHY_CFG_AHB2PHY_CBCR   0x6A004
#define GCC_AGGRE2_USB3_AXI_CBCR        0x83014
#define GCC_USB30_MASTER_CMD_RCGR       0xF014
#define GCC_USB30_MASTER_CFG_RCGR       0xF018

// UFS
#define GCC_UFS_AXI_CBCR                0x75008
#define GCC_UFS_AHB_CBCR                0x7500C
#define GCC_UFS_TX_CFG_CBCR             0x75010
#define GCC_UFS_RX_CFG_CBCR             0x75014
#define GCC_UFS_TX_SYMBOL_0_CBCR        0x75018
#define GCC_UFS_RX_SYMBOL_0_CBCR        0x7501C
#define GCC_UFS_RX_SYMBOL_1_CBCR        0x75020
#define GCC_UFS_ICE_CORE_CBCR           0x76004
#define GCC_UFS_AXI_CMD_RCGR            0x75024
#define GCC_UFS_AXI_CFG_RCGR            0x75028

// Display (MDSS)
#define GCC_MDSS_AHB_CBCR               0x4D07C
#define GCC_MDSS_AXI_CBCR               0x4D080
#define GCC_MDSS_MDP_CBCR               0x4D088
#define GCC_MDSS_EXTPCLK_CBCR           0x4D084
#define GCC_MDSS_VSYNC_CBCR             0x4D090
#define GCC_MDSS_MDP_CMD_RCGR           0x4D014
#define GCC_MDSS_MDP_CFG_RCGR           0x4D018
#define GCC_DSI0_ESC_CBCR               0x4D098
#define GCC_DSI1_ESC_CBCR               0x4D09C
#define GCC_DSI0_ESC_CMD_RCGR           0x4D05C
#define GCC_DSI1_ESC_CMD_RCGR           0x4D068

// HDMI
#define GCC_HDMI_AHB_CBCR               0x4D07C   // shared with MDSS_AHB
#define GCC_HDMI_CORE_CBCR              0x4D0A4
#define GCC_HDMI_EXTP_CBCR              0x4D0A8
#define GCC_HDMI_CORE_CMD_RCGR          0x4D070
#define GCC_HDMI_EXTP_CMD_RCGR          0x4D078

// PCIe
#define GCC_PCIE_0_AUX_CBCR             0x6B000
#define GCC_PCIE_0_CFG_AHB_CBCR        0x6B004
#define GCC_PCIE_0_MSTR_AXI_CBCR       0x6B008
#define GCC_PCIE_0_SLV_AXI_CBCR        0x6B00C
#define GCC_PCIE_0_PIPE_CBCR            0x6B010
#define GCC_PCIE_1_AUX_CBCR             0x6D000
#define GCC_PCIE_1_CFG_AHB_CBCR        0x6D004
#define GCC_PCIE_1_MSTR_AXI_CBCR       0x6D008
#define GCC_PCIE_1_SLV_AXI_CBCR        0x6D00C
#define GCC_PCIE_1_PIPE_CBCR            0x6D010
#define GCC_PCIE_2_AUX_CBCR             0x6E000
#define GCC_PCIE_2_CFG_AHB_CBCR        0x6E004
#define GCC_PCIE_2_MSTR_AXI_CBCR       0x6E008
#define GCC_PCIE_2_SLV_AXI_CBCR        0x6E00C
#define GCC_PCIE_2_PIPE_CBCR            0x6E010

// Crypto
#define GCC_CE1_CLK_CBCR                0x41144
#define GCC_CE1_AXI_CLK_CBCR           0x41148
#define GCC_CE1_AHB_CLK_CBCR           0x4114C

// SPMI
#define GCC_SPMI_AHB_CLK_CBCR           0x4F004
#define GCC_SPMI_SER_CLK_CBCR           0x4F008

// CBCR register bits
#define CLK_CBCR_CLK_OFF                BIT31
#define CLK_CBCR_CLK_EN                 BIT0
#define CLK_CMD_RCGR_UPDATE             BIT0
#define CLK_CMD_RCGR_ROOT_OFF           BIT31

// CFG_RCGR source select: SRC_SEL bits [10:8], SRC_DIV bits [4:0]
#define CLK_CFG_SRC_CXO                 (0 << 8)
#define CLK_CFG_SRC_GPLL0               (1 << 8)
#define CLK_CFG_DIV_1                   0
#define CLK_CFG_DIV_2                   1
#define CLK_CFG_DIV_4                   3
#define CLK_CFG_MODE_BYPASS             (0 << 12)
#define CLK_CFG_MODE_MND                (2 << 12)

STATIC UINT64  mGccBase = MSM8996_GCC_BASE;

STATIC
VOID
GccEnableClock (
  IN UINT32  Cbcr
  )
{
  UINT32  Val;

  Val  = MmioRead32 (mGccBase + Cbcr);
  Val |= CLK_CBCR_CLK_EN;
  MmioWrite32 (mGccBase + Cbcr, Val);

  // Poll for clock enable acknowledgement
  UINTN  Timeout = 500;
  while (Timeout-- && (MmioRead32 (mGccBase + Cbcr) & CLK_CBCR_CLK_OFF)) {
    // spin-wait ~1 μs equivalent
    MemoryFence ();
  }
  if (MmioRead32 (mGccBase + Cbcr) & CLK_CBCR_CLK_OFF) {
    DEBUG ((DEBUG_WARN, "GCC: CLK @0x%X still gated after enable\n", Cbcr));
  }
}

STATIC
VOID
GccUpdateRcgr (
  IN UINT32  CmdRcgr
  )
{
  MmioOr32 (mGccBase + CmdRcgr, CLK_CMD_RCGR_UPDATE);
  UINTN  Timeout = 500;
  while (Timeout-- && (MmioRead32 (mGccBase + CmdRcgr) & CLK_CMD_RCGR_UPDATE)) {
    MemoryFence ();
  }
}

/**
  Configure and enable the debug UART clock.

  BLSP2 UART1 (serial@075b0000) — 7.372800 MHz source required for 115200 baud.
  GCC GPLL0 = 600 MHz, divided by ~81 → 7.4074 MHz (close enough with MND).
  Actual MND values for 7.3728 MHz from 600 MHz source:
    M=144, N=15625, D=15625/2 — standard MSM UART clock setup.
  LK uses the BLSP2 UART1 clock; we replicate the same config.
**/
STATIC
VOID
GccConfigureDebugUart (
  VOID
  )
{
  // Enable BLSP2 AHB gating
  GccEnableClock (GCC_BLSP2_AHB_CBCR);

  // Configure BLSP2 UART1 apps clock for 7.3728 MHz (115200 × 64)
  // Source: CXO (19.2 MHz), mode: MND
  // 7.3728 MHz from 19.2 MHz: M=192, N=500, D=500  → 19.2 × 192/500 = 7.3728 MHz
  MmioWrite32 (mGccBase + GCC_BLSP2_UART1_APPS_CFG_RCGR,
               CLK_CFG_SRC_CXO | CLK_CFG_MODE_MND);
  MmioWrite32 (mGccBase + GCC_BLSP2_UART1_APPS_M, 192);
  MmioWrite32 (mGccBase + GCC_BLSP2_UART1_APPS_N, (UINT32)(~(500 - 192)));
  MmioWrite32 (mGccBase + GCC_BLSP2_UART1_APPS_D, (UINT32)(~500));
  GccUpdateRcgr (GCC_BLSP2_UART1_APPS_CMD_RCGR);
  GccEnableClock (GCC_BLSP2_UART1_APPS_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: BLSP2 UART1 clock configured (7.3728 MHz)\n"));
}

/**
  Enable all clocks needed for UFS.
**/
STATIC
VOID
GccEnableUfsClocks (
  VOID
  )
{
  // UFS AXI: source GPLL0 / 4 = 150 MHz
  MmioWrite32 (mGccBase + GCC_UFS_AXI_CFG_RCGR,
               CLK_CFG_SRC_GPLL0 | CLK_CFG_DIV_4);
  GccUpdateRcgr (GCC_UFS_AXI_CMD_RCGR);

  GccEnableClock (GCC_UFS_AXI_CBCR);
  GccEnableClock (GCC_UFS_AHB_CBCR);
  GccEnableClock (GCC_UFS_TX_CFG_CBCR);
  GccEnableClock (GCC_UFS_RX_CFG_CBCR);
  GccEnableClock (GCC_UFS_TX_SYMBOL_0_CBCR);
  GccEnableClock (GCC_UFS_RX_SYMBOL_0_CBCR);
  GccEnableClock (GCC_UFS_RX_SYMBOL_1_CBCR);
  GccEnableClock (GCC_UFS_ICE_CORE_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: UFS clocks enabled\n"));
}

/**
  Enable all clocks needed for USB 3.0.
**/
STATIC
VOID
GccEnableUsb3Clocks (
  VOID
  )
{
  // USB30 master: source GPLL0 / 4 = 150 MHz
  MmioWrite32 (mGccBase + GCC_USB30_MASTER_CFG_RCGR,
               CLK_CFG_SRC_GPLL0 | CLK_CFG_DIV_4);
  GccUpdateRcgr (GCC_USB30_MASTER_CMD_RCGR);

  GccEnableClock (GCC_USB30_MASTER_CBCR);
  GccEnableClock (GCC_USB30_SLEEP_CBCR);
  GccEnableClock (GCC_USB30_MOCK_UTMI_CBCR);
  GccEnableClock (GCC_USB30_PHY_AUX_CBCR);
  GccEnableClock (GCC_USB30_PIPE_CBCR);
  GccEnableClock (GCC_USB_PHY_CFG_AHB2PHY_CBCR);
  GccEnableClock (GCC_AGGRE2_USB3_AXI_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: USB3 clocks enabled\n"));
}

/**
  Enable all clocks needed for MDSS (display).
**/
STATIC
VOID
GccEnableDisplayClocks (
  VOID
  )
{
  GccEnableClock (GCC_MDSS_AHB_CBCR);
  GccEnableClock (GCC_MDSS_AXI_CBCR);

  // MDP clock: GPLL0 / 2 = 300 MHz (enough for WQXGA)
  MmioWrite32 (mGccBase + GCC_MDSS_MDP_CFG_RCGR,
               CLK_CFG_SRC_GPLL0 | CLK_CFG_DIV_2);
  GccUpdateRcgr (GCC_MDSS_MDP_CMD_RCGR);
  GccEnableClock (GCC_MDSS_MDP_CBCR);
  GccEnableClock (GCC_MDSS_VSYNC_CBCR);

  // DSI escape clocks from CXO (19.2 MHz), no division needed
  GccEnableClock (GCC_DSI0_ESC_CBCR);
  GccEnableClock (GCC_DSI1_ESC_CBCR);

  // HDMI
  GccEnableClock (GCC_HDMI_CORE_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: Display/MDSS clocks enabled\n"));
}

/**
  Enable PCIe clocks (needed for QCA6174 WiFi on PCIe0).
**/
STATIC
VOID
GccEnablePcieClocks (
  VOID
  )
{
  GccEnableClock (GCC_PCIE_0_AUX_CBCR);
  GccEnableClock (GCC_PCIE_0_CFG_AHB_CBCR);
  GccEnableClock (GCC_PCIE_0_MSTR_AXI_CBCR);
  GccEnableClock (GCC_PCIE_0_SLV_AXI_CBCR);
  GccEnableClock (GCC_PCIE_0_PIPE_CBCR);

  GccEnableClock (GCC_PCIE_1_AUX_CBCR);
  GccEnableClock (GCC_PCIE_1_CFG_AHB_CBCR);
  GccEnableClock (GCC_PCIE_1_MSTR_AXI_CBCR);
  GccEnableClock (GCC_PCIE_1_SLV_AXI_CBCR);
  GccEnableClock (GCC_PCIE_1_PIPE_CBCR);

  GccEnableClock (GCC_PCIE_2_AUX_CBCR);
  GccEnableClock (GCC_PCIE_2_CFG_AHB_CBCR);
  GccEnableClock (GCC_PCIE_2_MSTR_AXI_CBCR);
  GccEnableClock (GCC_PCIE_2_SLV_AXI_CBCR);
  GccEnableClock (GCC_PCIE_2_PIPE_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: PCIe0/1/2 clocks enabled\n"));
}

/**
  Enable crypto engine clocks.
**/
STATIC
VOID
GccEnableCryptoClocks (
  VOID
  )
{
  GccEnableClock (GCC_CE1_CLK_CBCR);
  GccEnableClock (GCC_CE1_AXI_CLK_CBCR);
  GccEnableClock (GCC_CE1_AHB_CLK_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: Crypto clocks enabled\n"));
}

/**
  Enable SPMI (PMIC bus) clocks.
**/
STATIC
VOID
GccEnableSpmiClocks (
  VOID
  )
{
  GccEnableClock (GCC_SPMI_AHB_CLK_CBCR);
  GccEnableClock (GCC_SPMI_SER_CLK_CBCR);

  DEBUG ((DEBUG_INFO, "GCC: SPMI clocks enabled\n"));
}

/**
  Entry point for GCC Clock DXE driver.
**/
EFI_STATUS
EFIAPI
GccClockDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "GccClockDxe: Initializing MSM8996 GCC @ 0x%08X\n",
          MSM8996_GCC_BASE));

  GccConfigureDebugUart ();
  GccEnableSpmiClocks ();
  GccEnableUfsClocks ();
  GccEnableUsb3Clocks ();
  GccEnableDisplayClocks ();
  GccEnablePcieClocks ();
  GccEnableCryptoClocks ();

  DEBUG ((DEBUG_INFO, "GccClockDxe: All hardware clocks enabled\n"));
  return EFI_SUCCESS;
}
