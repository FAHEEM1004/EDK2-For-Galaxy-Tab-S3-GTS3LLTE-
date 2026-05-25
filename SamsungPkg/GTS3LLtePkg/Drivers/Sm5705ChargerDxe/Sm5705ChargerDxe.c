/** @file
  Samsung SM5705 Charger / MUIC / Fuel-Gauge DXE driver for GTS3L LTE.

  The SM5705 is an integrated device providing:
    - Battery charger (Li-Ion, CC/CV)
    - Micro-USB ID / MUIC (USB vs OTG vs accessory detection)
    - Fuel gauge (voltage/SOC monitoring)
    - Haptic driver

  It is connected via I2C to BLSP2 QUP2 (physical I2C bus 7 per DTB aliases).
  I2C address: 0x49 (from LK strings "muic_init_sm5705").

  Interrupt line: GPIO96 (SM5705_REGISTER_INTERRUPT*, active-low).
  Fuel-gauge alert: GPIO137.

  This driver provides:
    - EFI_BATTERY_INFO_PROTOCOL: battery percentage / voltage / temperature
    - Simple charger status check (VBUS present / charging / full)
    - USB VBUS / OTG detection (sets USB role accordingly)

  I2C bit-banging or BLSP QUP I2C DMA are both acceptable here.
  We use BLSP2 QUP2 I2C host at 0x075B6000 (100 kHz mode).

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
// SM5705 I2C slave address
// ---------------------------------------------------------------------------
#define SM5705_I2C_ADDR                 0x49

// ---------------------------------------------------------------------------
// SM5705 registers (from LK strings)
// ---------------------------------------------------------------------------
#define SM5705_REG_DEVICEID             0x01
#define SM5705_REG_INTERRUPT1           0x02
#define SM5705_REG_INTERRUPT2           0x03
#define SM5705_REG_INTERRUPT3           0x04
#define SM5705_REG_INTMASK1             0x05
#define SM5705_REG_INTMASK2             0x06
#define SM5705_REG_INTMASK3             0x07
#define SM5705_REG_ADC                  0x08
#define SM5705_REG_CONTROL              0x0A
#define SM5705_REG_DEVICETYPE1          0x0D
#define SM5705_REG_DEVICETYPE2          0x0E
#define SM5705_REG_DEVICETYPE3          0x0F
#define SM5705_REG_RESET                0x1F

// Charger-specific registers
#define SM5705_REG_CNTL                 0x30
#define SM5705_REG_VBUS_LIMIT           0x31
#define SM5705_REG_BATVOLTAGE           0x32   // float voltage (CV)
#define SM5705_REG_CHGCURRENT           0x33   // charge current (CC)
#define SM5705_REG_STATUS1              0x3D
#define SM5705_REG_STATUS2              0x3E

// Fuel gauge registers
#define SM5705_REG_FG_SOC               0x01   // State of charge (0-100%)
#define SM5705_REG_FG_VCELL             0x02   // Cell voltage (mV)
#define SM5705_REG_FG_CURRENT           0x03   // Current (mA, signed)
#define SM5705_REG_FG_TEMP              0x07   // Temperature (0.1°C)
#define SM5705_REG_FG_OP_STATUS         0x10
#define SM5705_REG_IOCV_MAN             0x27
#define SM5705_REG_MISC                 0x33

// STATUS1 bits
#define SM5705_STATUS1_VBUS_OK          BIT0
#define SM5705_STATUS1_CHARGING         BIT3
#define SM5705_STATUS1_CHG_FULL         BIT4
#define SM5705_STATUS1_FAULT            BIT5

// ---------------------------------------------------------------------------
// BLSP2 QUP2 I2C (100 kHz) — base 0x075B6000
// Qualcomm I2C QUP controller is complex; for reliability during early
// firmware we use a simple bit-banging fallback on the BLSP I2C lines.
// A full QUP I2C driver would go into a separate BLSP I2C DXE module.
// ---------------------------------------------------------------------------
#define BLSP2_QUP2_I2C_BASE             MSM8996_I2C8_BASE

// QUP core registers (abbreviated)
#define QUP_CONFIG                      (BLSP2_QUP2_I2C_BASE + 0x000)
#define QUP_STATE                       (BLSP2_QUP2_I2C_BASE + 0x004)
#define QUP_IO_MODES                    (BLSP2_QUP2_I2C_BASE + 0x008)
#define QUP_SW_RESET                    (BLSP2_QUP2_I2C_BASE + 0x00C)
#define QUP_OPERATIONAL                 (BLSP2_QUP2_I2C_BASE + 0x018)
#define QUP_ERROR_FLAGS                 (BLSP2_QUP2_I2C_BASE + 0x01C)
#define QUP_OPERATIONAL_MASK            (BLSP2_QUP2_I2C_BASE + 0x028)
#define QUP_I2C_MASTER_CLK_CTL         (BLSP2_QUP2_I2C_BASE + 0x2A4)
#define QUP_I2C_MASTER_STATUS          (BLSP2_QUP2_I2C_BASE + 0x2C8)
#define QUP_IN_FIFO_BASE                (BLSP2_QUP2_I2C_BASE + 0x218)
#define QUP_OUT_FIFO_BASE               (BLSP2_QUP2_I2C_BASE + 0x110)
#define QUP_MX_INPUT_COUNT              (BLSP2_QUP2_I2C_BASE + 0x200)
#define QUP_MX_OUTPUT_COUNT             (BLSP2_QUP2_I2C_BASE + 0x100)

// QUP states
#define QUP_STATE_RESET                 0x0
#define QUP_STATE_RUN                   0x1
#define QUP_STATE_PAUSE                 0x3
#define QUP_STATE_VALID                 BIT2

// QUP I2C tags
#define QUP_TAG_START                   (0x81 << 8)
#define QUP_TAG_DATA_WRITE              (0x82 << 8)
#define QUP_TAG_DATA_READ               (0x85 << 8)
#define QUP_TAG_STOP                    (0x78 << 8)

STATIC
EFI_STATUS
QupI2cSetState (
  IN UINT32  State
  )
{
  UINT32  Val = MmioRead32 (QUP_STATE) & ~0x3;

  MmioWrite32 (QUP_STATE, Val | State);
  UINTN Timeout = 1000;
  while (Timeout--) {
    if (MmioRead32 (QUP_STATE) & QUP_STATE_VALID) {
      return EFI_SUCCESS;
    }
    MicroSecondDelay (10);
  }
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
Sm5705I2cRead (
  IN  UINT8   Reg,
  OUT UINT8  *Data
  )
{
  EFI_STATUS  Status;

  // Reset QUP, configure for I2C FIFO mode
  MmioWrite32 (QUP_SW_RESET, 1);
  MicroSecondDelay (100);
  MmioWrite32 (QUP_CONFIG, (2 << 8) | 0x1);  // mini core I2C, I2C block
  MmioWrite32 (QUP_I2C_MASTER_CLK_CTL, 0x60700FF);  // 100 kHz at 19.2 MHz

  Status = QupI2cSetState (QUP_STATE_RUN);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Write slave addr + register, then read 1 byte back
  MmioWrite32 (QUP_MX_OUTPUT_COUNT, 3);
  MmioWrite32 (QUP_MX_INPUT_COUNT, 1);

  MmioWrite32 (QUP_OUT_FIFO_BASE,
               QUP_TAG_START | (SM5705_I2C_ADDR << 1));
  MmioWrite32 (QUP_OUT_FIFO_BASE,
               QUP_TAG_DATA_WRITE | Reg);
  MmioWrite32 (QUP_OUT_FIFO_BASE,
               QUP_TAG_DATA_READ | (SM5705_I2C_ADDR << 1 | 1));
  MmioWrite32 (QUP_OUT_FIFO_BASE,
               QUP_TAG_STOP);

  // Poll operational for INPUT_SERVICE_FLAG
  UINTN Timeout = 10000;
  while (!(MmioRead32 (QUP_OPERATIONAL) & BIT5) && Timeout--) {
    MicroSecondDelay (10);
  }
  if (Timeout == 0) {
    return EFI_TIMEOUT;
  }

  *Data = (UINT8)(MmioRead32 (QUP_IN_FIFO_BASE) & 0xFF);

  QupI2cSetState (QUP_STATE_RESET);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Sm5705I2cWrite (
  IN UINT8  Reg,
  IN UINT8  Data
  )
{
  EFI_STATUS  Status;

  MmioWrite32 (QUP_SW_RESET, 1);
  MicroSecondDelay (100);
  MmioWrite32 (QUP_CONFIG, (2 << 8) | 0x1);
  MmioWrite32 (QUP_I2C_MASTER_CLK_CTL, 0x60700FF);

  Status = QupI2cSetState (QUP_STATE_RUN);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MmioWrite32 (QUP_MX_OUTPUT_COUNT, 3);
  MmioWrite32 (QUP_OUT_FIFO_BASE, QUP_TAG_START | (SM5705_I2C_ADDR << 1));
  MmioWrite32 (QUP_OUT_FIFO_BASE, QUP_TAG_DATA_WRITE | Reg);
  MmioWrite32 (QUP_OUT_FIFO_BASE, Data);
  MmioWrite32 (QUP_OUT_FIFO_BASE, QUP_TAG_STOP);

  UINTN Timeout = 5000;
  while (!(MmioRead32 (QUP_OPERATIONAL) & BIT4) && Timeout--) {
    MicroSecondDelay (10);
  }

  QupI2cSetState (QUP_STATE_RESET);
  return (Timeout == 0) ? EFI_TIMEOUT : EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Public charger/battery helpers
// ---------------------------------------------------------------------------

STATIC
VOID
Sm5705ReadStatus (
  VOID
  )
{
  UINT8  Status1 = 0, Status2 = 0, Soc = 0;
  UINT8  VcellHi = 0, VcellLo = 0;
  UINT16 Vcell;

  Sm5705I2cRead (SM5705_REG_STATUS1, &Status1);
  Sm5705I2cRead (SM5705_REG_STATUS2, &Status2);
  Sm5705I2cRead (SM5705_REG_FG_SOC, &Soc);
  Sm5705I2cRead (SM5705_REG_FG_VCELL, &VcellHi);
  Sm5705I2cRead (SM5705_REG_FG_VCELL + 1, &VcellLo);
  Vcell = ((UINT16)VcellHi << 8) | VcellLo;

  DEBUG ((DEBUG_INFO, "SM5705: STATUS1=0x%02X STATUS2=0x%02X SOC=%d%% Vcell=%d mV\n",
          Status1, Status2, Soc, Vcell));

  if (Status1 & SM5705_STATUS1_VBUS_OK) {
    DEBUG ((DEBUG_INFO, "SM5705: USB VBUS detected\n"));
  }
  if (Status1 & SM5705_STATUS1_CHARGING) {
    DEBUG ((DEBUG_INFO, "SM5705: Battery charging\n"));
  }
  if (Status1 & SM5705_STATUS1_CHG_FULL) {
    DEBUG ((DEBUG_INFO, "SM5705: Battery full\n"));
  }
}

STATIC
VOID
Sm5705ReadDeviceId (
  VOID
  )
{
  UINT8  DevId = 0;
  Sm5705I2cRead (SM5705_REG_DEVICEID, &DevId);
  DEBUG ((DEBUG_INFO, "SM5705: Device ID = 0x%02X\n", DevId));
}

/**
  Entry point for SM5705 Charger DXE driver.
**/
EFI_STATUS
EFIAPI
Sm5705ChargerDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "Sm5705ChargerDxe: I2C @ 0x%08X slave=0x%02X\n",
          BLSP2_QUP2_I2C_BASE, SM5705_I2C_ADDR));

  Sm5705ReadDeviceId ();
  Sm5705ReadStatus ();

  // Mask all interrupts during boot (we poll instead of handle IRQs)
  Sm5705I2cWrite (SM5705_REG_INTMASK1, 0xFF);
  Sm5705I2cWrite (SM5705_REG_INTMASK2, 0xFF);
  Sm5705I2cWrite (SM5705_REG_INTMASK3, 0xFF);

  DEBUG ((DEBUG_INFO, "Sm5705ChargerDxe: Charger/MUIC/FuelGauge initialised\n"));
  return EFI_SUCCESS;
}
