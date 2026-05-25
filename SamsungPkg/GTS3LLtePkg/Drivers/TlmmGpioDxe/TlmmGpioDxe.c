/** @file
  TLMM (Top Level Mode Multiplexer) GPIO driver for MSM8996.

  Controls all 150 GPIOs on the GTS3L LTE. Each GPIO has:
    - TLMM_GPIO_CFGn   @ base + n*0x1000        (config: func, dir, pull, drive)
    - TLMM_GPIO_IN_OUTn @ base + n*0x1000 + 0x4 (input/output value)
    - TLMM_GPIO_INTR_CFGn, INTR_STATUS, INTR_TARGET (interrupt config)

  Base address: 0x01000000 (verified from XBL MMIO table, "TLMM CSR").

  GPIO function select values (TLMM_GPIO_CFGn[5:2]):
    0 = GPIO
    1..15 = alternate function (UART, I2C, SPI, USB, DSI, HDMI, etc.)

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/EmbeddedGpio.h>
#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// Register layout
// ---------------------------------------------------------------------------
#define TLMM_GPIO_CFG(n)         (MSM8996_TLMM_BASE + (n) * 0x1000 + 0x000)
#define TLMM_GPIO_IN_OUT(n)      (MSM8996_TLMM_BASE + (n) * 0x1000 + 0x004)
#define TLMM_GPIO_INTR_CFG(n)    (MSM8996_TLMM_BASE + (n) * 0x1000 + 0x008)
#define TLMM_GPIO_INTR_STATUS(n) (MSM8996_TLMM_BASE + (n) * 0x1000 + 0x00C)

// TLMM_GPIO_CFGn bits
#define GPIO_CFG_PULL_NO        (0 << 0)
#define GPIO_CFG_PULL_DOWN      (1 << 0)
#define GPIO_CFG_PULL_KEEPER    (2 << 0)
#define GPIO_CFG_PULL_UP        (3 << 0)
#define GPIO_CFG_FUNC(f)        (((f) & 0xF) << 2)
#define GPIO_CFG_FUNC_GPIO      GPIO_CFG_FUNC(0)
#define GPIO_CFG_DRV_2MA        (0 << 6)
#define GPIO_CFG_DRV_4MA        (1 << 6)
#define GPIO_CFG_DRV_6MA        (2 << 6)
#define GPIO_CFG_DRV_8MA        (3 << 6)
#define GPIO_CFG_DRV_10MA       (4 << 6)
#define GPIO_CFG_DRV_12MA       (5 << 6)
#define GPIO_CFG_DRV_14MA       (6 << 6)
#define GPIO_CFG_DRV_16MA       (7 << 6)
#define GPIO_CFG_OE             BIT9        // output enable

// TLMM_GPIO_IN_OUTn bits
#define GPIO_OUT_VAL            BIT1
#define GPIO_IN_VAL             BIT0

// ---------------------------------------------------------------------------
// Known GPIO assignments from DTB / LK analysis
// ---------------------------------------------------------------------------
// SM5705 charger interrupt: gpio96
#define GPIO_CHARGER_IRQ        96
// SM5705 fuel-gauge alert: gpio137
#define GPIO_FUELGAUGE_IRQ      137
// USB ID / VBUS detect: typically gpio98
#define GPIO_USB_ID             98
// Display reset (NT35597 panel):
#define GPIO_PANEL_RESET        8
#define GPIO_PANEL_RESET2       25
// WLAN power: gpio13 (from wcnss_wlan node)
#define GPIO_WLAN_EN            13
// BT enable: from btfm node
#define GPIO_BT_EN              20
// Volume keys
#define GPIO_VOL_UP             73
#define GPIO_VOL_DOWN           74

// ---------------------------------------------------------------------------
// EmbeddedGpio protocol implementation
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
GpioGet (
  IN  EMBEDDED_GPIO       *This,
  IN  EMBEDDED_GPIO_PIN   Gpio,
  OUT UINTN               *Value
  )
{
  if (Gpio >= MSM8996_GPIO_COUNT) {
    return EFI_INVALID_PARAMETER;
  }
  *Value = (MmioRead32 (TLMM_GPIO_IN_OUT (Gpio)) & GPIO_IN_VAL) ? 1 : 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GpioSet (
  IN EMBEDDED_GPIO       *This,
  IN EMBEDDED_GPIO_PIN   Gpio,
  IN EMBEDDED_GPIO_MODE  Mode
  )
{
  if (Gpio >= MSM8996_GPIO_COUNT) {
    return EFI_INVALID_PARAMETER;
  }

  UINT32  Cfg = MmioRead32 (TLMM_GPIO_CFG (Gpio));

  switch (Mode) {
    case GPIO_MODE_INPUT:
      Cfg &= ~GPIO_CFG_OE;
      Cfg  = (Cfg & ~(0xF << 2)) | GPIO_CFG_FUNC_GPIO;
      MmioWrite32 (TLMM_GPIO_CFG (Gpio), Cfg);
      break;

    case GPIO_MODE_OUTPUT_0:
      Cfg |= GPIO_CFG_OE;
      Cfg  = (Cfg & ~(0xF << 2)) | GPIO_CFG_FUNC_GPIO;
      MmioWrite32 (TLMM_GPIO_CFG (Gpio), Cfg);
      MmioAnd32 (TLMM_GPIO_IN_OUT (Gpio), ~GPIO_OUT_VAL);
      break;

    case GPIO_MODE_OUTPUT_1:
      Cfg |= GPIO_CFG_OE;
      Cfg  = (Cfg & ~(0xF << 2)) | GPIO_CFG_FUNC_GPIO;
      MmioWrite32 (TLMM_GPIO_CFG (Gpio), Cfg);
      MmioOr32 (TLMM_GPIO_IN_OUT (Gpio), GPIO_OUT_VAL);
      break;

    default:
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GpioGetMode (
  IN  EMBEDDED_GPIO       *This,
  IN  EMBEDDED_GPIO_PIN   Gpio,
  OUT EMBEDDED_GPIO_MODE  *Mode
  )
{
  if (Gpio >= MSM8996_GPIO_COUNT) {
    return EFI_INVALID_PARAMETER;
  }

  UINT32  Cfg = MmioRead32 (TLMM_GPIO_CFG (Gpio));
  UINT32  Out = MmioRead32 (TLMM_GPIO_IN_OUT (Gpio));

  if (Cfg & GPIO_CFG_OE) {
    *Mode = (Out & GPIO_OUT_VAL) ? GPIO_MODE_OUTPUT_1 : GPIO_MODE_OUTPUT_0;
  } else {
    *Mode = GPIO_MODE_INPUT;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GpioSetPull (
  IN EMBEDDED_GPIO       *This,
  IN EMBEDDED_GPIO_PIN   Gpio,
  IN EMBEDDED_GPIO_PULL  Direction
  )
{
  if (Gpio >= MSM8996_GPIO_COUNT) {
    return EFI_INVALID_PARAMETER;
  }

  UINT32  Pull;
  switch (Direction) {
    case GPIO_PULL_UP:     Pull = GPIO_CFG_PULL_UP;   break;
    case GPIO_PULL_DOWN:   Pull = GPIO_CFG_PULL_DOWN; break;
    case GPIO_PULL_NONE:   Pull = GPIO_CFG_PULL_NO;   break;
    default:               Pull = GPIO_CFG_PULL_NO;   break;
  }

  MmioAndThenOr32 (TLMM_GPIO_CFG (Gpio), ~0x3U, Pull);
  return EFI_SUCCESS;
}

STATIC EMBEDDED_GPIO  mGpioProtocol = {
  GpioGet,
  GpioSet,
  GpioGetMode,
  GpioSetPull,
};

/**
  Configure hardware GPIOs for BLSP2 UART1 (GPIO 41=TX, 42=RX, AF=2).
**/
STATIC
VOID
ConfigureUartGpios (
  VOID
  )
{
  // BLSP2 UART1 TX = GPIO41, RX = GPIO42, alternate function 2
  MmioWrite32 (TLMM_GPIO_CFG (41),
               GPIO_CFG_FUNC (2) | GPIO_CFG_DRV_4MA | GPIO_CFG_PULL_NO | GPIO_CFG_OE);
  MmioWrite32 (TLMM_GPIO_CFG (42),
               GPIO_CFG_FUNC (2) | GPIO_CFG_DRV_4MA | GPIO_CFG_PULL_UP);
  DEBUG ((DEBUG_INFO, "TLMM: BLSP2 UART1 GPIOs configured (41=TX, 42=RX)\n"));
}

/**
  Configure GPIOs for SM5705 charger and fuel gauge.
**/
STATIC
VOID
ConfigureChargerGpios (
  VOID
  )
{
  // Charger IRQ pin — input, pull-up
  MmioWrite32 (TLMM_GPIO_CFG (GPIO_CHARGER_IRQ),
               GPIO_CFG_FUNC_GPIO | GPIO_CFG_PULL_UP);
  // Fuel gauge alert pin — input, pull-up
  MmioWrite32 (TLMM_GPIO_CFG (GPIO_FUELGAUGE_IRQ),
               GPIO_CFG_FUNC_GPIO | GPIO_CFG_PULL_UP);
  DEBUG ((DEBUG_INFO, "TLMM: Charger/FuelGauge GPIOs configured\n"));
}

/**
  Entry point for TLMM GPIO DXE driver.
**/
EFI_STATUS
EFIAPI
TlmmGpioDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle = NULL;

  DEBUG ((DEBUG_INFO, "TlmmGpioDxe: MSM8996 TLMM @ 0x%08X, %d GPIOs\n",
          MSM8996_TLMM_BASE, MSM8996_GPIO_COUNT));

  ConfigureUartGpios ();
  ConfigureChargerGpios ();

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEmbeddedGpioProtocolGuid,
                  &mGpioProtocol,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "TlmmGpioDxe: EmbeddedGpio protocol installed\n"));
  return EFI_SUCCESS;
}
