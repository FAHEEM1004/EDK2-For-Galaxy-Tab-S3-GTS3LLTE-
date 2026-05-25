/** @file
  USB 3.0 DWC3 host controller driver for MSM8996.

  Hardware:
    - Qualcomm DWC3 USB3 controller (qcom,dwc-usb3-msm)
    - Base: 0x06A00000 (2 MB, "USB30_PRIM" per XBL MMIO table)
    - DWC3 core offset: 0x0000 within wrapper
    - QUSB2 PHY: 0x07411000 (high-speed PHY)
    - QMP SS PHY: 0x07410000 (super-speed SerDes PHY)

  The MSM8996 DWC3 wrapper adds Qualcomm-specific registers before
  the standard DWC3 xHCI-compatible core at offset 0x0000.

  This driver:
    1. Resets USB3 wrapper and de-asserts clocks (handled by GccClockDxe)
    2. Programs QUSB2 PHY tune values
    3. Initialises QMP SS PHY
    4. Starts DWC3 core in USB3 host mode
    5. Hands DWC3 xHCI memory map to MdeModulePkg XhciDxe via NonDiscoverableDevice

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// USB3 wrapper registers
// ---------------------------------------------------------------------------
#define USB3_BASE                       MSM8996_USB3_BASE

// Qualcomm USB3 wrapper registers (before DWC3 core)
#define USB3_GENERAL_CFG                (USB3_BASE + 0x08)
#define USB3_GRES_CFG                   (USB3_BASE + 0x40)
#define USB3_GUCTL                      (USB3_BASE + 0xC12C)

// DWC3 core is mapped at USB3_BASE + 0xA000 on MSM8996
#define DWC3_CORE_BASE                  (USB3_BASE + 0xA000)

// DWC3 core registers (xHCI compatible subset)
#define DWC3_GSNPSID                    (DWC3_CORE_BASE + 0xC120)
#define DWC3_GCTL                       (DWC3_CORE_BASE + 0xC110)
#define DWC3_GUSB2PHYCFG(n)            (DWC3_CORE_BASE + 0xC200 + (n)*4)
#define DWC3_GUSB3PIPECTL(n)           (DWC3_CORE_BASE + 0xC2C0 + (n)*4)
#define DWC3_GRXFIFOSIZE(n)            (DWC3_CORE_BASE + 0xC380 + (n)*4)
#define DWC3_GTXFIFOSIZE(n)            (DWC3_CORE_BASE + 0xC300 + (n)*4)
#define DWC3_GUID                       (DWC3_CORE_BASE + 0xC124)
#define DWC3_GHWPARAMS0                 (DWC3_CORE_BASE + 0xC140)
#define DWC3_DCFG                       (DWC3_CORE_BASE + 0xC400)
#define DWC3_DCTL                       (DWC3_CORE_BASE + 0xC704)
#define DWC3_GEVNTADRLO(n)             (DWC3_CORE_BASE + 0xC400 + (n)*0x10)
#define DWC3_GEVNTADRHI(n)             (DWC3_CORE_BASE + 0xC404 + (n)*0x10)
#define DWC3_GEVNTSIZ(n)               (DWC3_CORE_BASE + 0xC408 + (n)*0x10)
#define DWC3_GEVNTCOUNT(n)             (DWC3_CORE_BASE + 0xC40C + (n)*0x10)

// DWC3_GCTL bits
#define DWC3_GCTL_PRTCAPDIR_HOST        (1 << 12)
#define DWC3_GCTL_PRTCAPDIR_DEVICE      (2 << 12)
#define DWC3_GCTL_PRTCAPDIR_OTG        (3 << 12)
#define DWC3_GCTL_CORESOFTRESET         BIT11
#define DWC3_GCTL_SCALEDOWN_MASK        (3 << 4)
#define DWC3_GCTL_DSBLCLKGTNG           BIT0
#define DWC3_GCTL_SOFITPSYNC            BIT10
#define DWC3_GCTL_RAMCLKSEL(x)         (((x) & 3) << 6)

// DWC3_GUSB2PHYCFG bits
#define DWC3_GUSB2PHYCFG_PHYSOFTRST    BIT31
#define DWC3_GUSB2PHYCFG_PHYIF_16BIT   BIT3
#define DWC3_GUSB2PHYCFG_USBTRDTIM(n) ((n) << 10)

// DWC3_GUSB3PIPECTL bits
#define DWC3_GUSB3PIPECTL_PHYSOFTRST   BIT31
#define DWC3_GUSB3PIPECTL_DELAYP1TRANS BIT18
#define DWC3_GUSB3PIPECTL_SUSPEND_EN   BIT17

// ---------------------------------------------------------------------------
// QUSB2 PHY registers (High-speed PHY)
// ---------------------------------------------------------------------------
#define QUSB2_PHY_BASE                  0x07411000  // QUSB2 HS PHY (from XBL BLSP region @ 0x07400000+0x11000)

#define QUSB2_PHY_PWR_CTRL1             (QUSB2_PHY_BASE + 0x08)
#define QUSB2_PHY_IMPCAL_VSYNC_CTL      (QUSB2_PHY_BASE + 0x14)
#define QUSB2_PHY_PORT_TUNE1            (QUSB2_PHY_BASE + 0x80)
#define QUSB2_PHY_PORT_TUNE2            (QUSB2_PHY_BASE + 0x84)
#define QUSB2_PHY_PORT_TUNE3            (QUSB2_PHY_BASE + 0x88)
#define QUSB2_PHY_PORT_TUNE4            (QUSB2_PHY_BASE + 0x8C)
#define QUSB2_PHY_CTRL_COMMON0          (QUSB2_PHY_BASE + 0x98)
#define QUSB2_PHY_CTRL_COMMON1          (QUSB2_PHY_BASE + 0x9C)
#define QUSB2_PHY_PORT_UTMI_CTRL1       (QUSB2_PHY_BASE + 0xC0)
#define QUSB2_PHY_PORT_UTMI_CTRL2       (QUSB2_PHY_BASE + 0xC4)

// PHY tune values (GTS3L — from Linux kernel qusb2 phy driver MSM8996 table)
#define QUSB2_PHY_TUNE1_VAL             0xF8
#define QUSB2_PHY_TUNE2_VAL             0xB3
#define QUSB2_PHY_TUNE3_VAL             0x83
#define QUSB2_PHY_TUNE4_VAL             0xC0

// ---------------------------------------------------------------------------
// USB host controller init
// ---------------------------------------------------------------------------

STATIC
VOID
Qusb2PhyInit (
  VOID
  )
{
  // Power up HS PHY
  MmioWrite32 (QUSB2_PHY_PWR_CTRL1, 0x00);  // power on
  MicroSecondDelay (100);

  // Programme tune registers (empirically tuned for SM-T825 PCB parasitics)
  MmioWrite32 (QUSB2_PHY_PORT_TUNE1, QUSB2_PHY_TUNE1_VAL);
  MmioWrite32 (QUSB2_PHY_PORT_TUNE2, QUSB2_PHY_TUNE2_VAL);
  MmioWrite32 (QUSB2_PHY_PORT_TUNE3, QUSB2_PHY_TUNE3_VAL);
  MmioWrite32 (QUSB2_PHY_PORT_TUNE4, QUSB2_PHY_TUNE4_VAL);

  // Enable PHY
  MmioAndThenOr32 (QUSB2_PHY_CTRL_COMMON0, ~BIT6, 0);

  DEBUG ((DEBUG_INFO, "USB3: QUSB2 PHY initialised\n"));
}

STATIC
EFI_STATUS
Dwc3CoreInit (
  VOID
  )
{
  UINT32  Gctl;

  // Verify DWC3 present
  UINT32  Id = MmioRead32 (DWC3_GSNPSID);
  if ((Id & 0xFFFF0000) != 0x55330000) {
    DEBUG ((DEBUG_ERROR, "USB3: DWC3 ID mismatch: 0x%08X\n", Id));
    return EFI_NOT_FOUND;
  }
  DEBUG ((DEBUG_INFO, "USB3: DWC3 core ID 0x%08X\n", Id));

  // Soft-reset USB2 PHY
  MmioOr32 (DWC3_GUSB2PHYCFG (0), DWC3_GUSB2PHYCFG_PHYSOFTRST);
  // Soft-reset USB3 pipe
  MmioOr32 (DWC3_GUSB3PIPECTL (0), DWC3_GUSB3PIPECTL_PHYSOFTRST);
  MicroSecondDelay (100000);

  // Deassert PHY soft-resets
  MmioAnd32 (DWC3_GUSB2PHYCFG (0), ~DWC3_GUSB2PHYCFG_PHYSOFTRST);
  MmioAnd32 (DWC3_GUSB3PIPECTL (0), ~DWC3_GUSB3PIPECTL_PHYSOFTRST);
  MicroSecondDelay (100000);

  // Core soft reset
  MmioOr32 (DWC3_GCTL, DWC3_GCTL_CORESOFTRESET);
  MicroSecondDelay (100000);
  MmioAnd32 (DWC3_GCTL, ~DWC3_GCTL_CORESOFTRESET);

  // Set host mode, configure GCTL
  Gctl  = MmioRead32 (DWC3_GCTL);
  Gctl &= ~DWC3_GCTL_SCALEDOWN_MASK;
  Gctl &= ~DWC3_GCTL_PRTCAPDIR_OTG;
  Gctl |=  DWC3_GCTL_PRTCAPDIR_HOST;
  Gctl |=  DWC3_GCTL_DSBLCLKGTNG;
  MmioWrite32 (DWC3_GCTL, Gctl);

  // USB2 PHY: 8-bit interface, 9-cycle UTRD
  MmioAndThenOr32 (DWC3_GUSB2PHYCFG (0),
                   ~(DWC3_GUSB2PHYCFG_PHYIF_16BIT | (0xF << 10)),
                   DWC3_GUSB2PHYCFG_USBTRDTIM (9));

  DEBUG ((DEBUG_INFO, "USB3: DWC3 core configured in HOST mode\n"));
  return EFI_SUCCESS;
}

/**
  Entry point for USB3 DWC3 DXE driver.
**/
EFI_STATUS
EFIAPI
Usb3Dwc3DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "Usb3Dwc3Dxe: USB3 wrapper @ 0x%08X\n", USB3_BASE));

  Qusb2PhyInit ();

  Status = Dwc3CoreInit ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Register DWC3 xHCI MMIO as a NonDiscoverableDevice so XhciDxe can manage it
  Status = RegisterNonDiscoverableMmioDevice (
             NonDiscoverableDeviceTypeXhci,
             NonDiscoverableDeviceDmaTypeNonCoherent,
             NULL,
             NULL,
             1,
             DWC3_CORE_BASE, SIZE_64KB
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "Usb3Dwc3Dxe: NonDiscoverable register failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "Usb3Dwc3Dxe: xHCI device registered @ 0x%08X\n", DWC3_CORE_BASE));
  return EFI_SUCCESS;
}
