/** @file
  SPMI (System Power Management Interface) and PMIC driver for MSM8996.

  Manages the SPMI arbiter bus and provides access to:
    - PM8994  (main PMIC, slave 0/1)  — LDOs, SMPSes, GPIOs, coincell, MPP
    - PMI8994 (companion PMIC, slave 2/3) — fuel-gauge, charger, flash LEDs, haptics

  SPMI arbiter base: 0x04000000 (28 MB, from XBL MMIO table).

  SPMI arbiter register layout (MSM8996 v2):
    SPMI_GENI_CFG_RAM_ADDR        = base + 0x0A000
    SPMI_GENI_CFG_RAM_DATA        = base + 0x0A004
    SPMI_MASTER_REG_BASE          = base + 0x400000
    SPMI_MASTER_REG(n)            = MASTER_REG_BASE + n * 0x1000
    CMD_OPCODE offset             = 0x000
    CMD_ADDR offset               = 0x004
    CMD_DATA offset               = 0x008
    STATUS offset                 = 0x00C
    OBS_SLAVE_BASE                = base + 0x00E00000

  SM5705 MUIC/Charger is connected via I2C to BLSP2 QUP2 (not SPMI).
  The PM8994 and PMI8994 are on SPMI bus.

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
// SPMI Arbiter register map
// ---------------------------------------------------------------------------
#define SPMI_BASE                       MSM8996_SPMI_BASE

// SPMI PIC (interrupt controller)
#define SPMI_PIC_OWNERm_ACC_STATUSn(m,n)  (SPMI_BASE + 0x700000 + (m)*0x1000 + (n)*4)

// SPMI arbiter channel (one per PMIC peripheral)
#define SPMI_CH_OFFSET(ch)              ((ch) * 0x8000)
#define SPMI_CH_CMD0(ch)                (SPMI_BASE + 0x400000 + SPMI_CH_OFFSET(ch) + 0x00)
#define SPMI_CH_CMD1(ch)                (SPMI_BASE + 0x400000 + SPMI_CH_OFFSET(ch) + 0x04)
#define SPMI_CH_CMD2(ch)                (SPMI_BASE + 0x400000 + SPMI_CH_OFFSET(ch) + 0x08)
#define SPMI_CH_CMD3(ch)                (SPMI_BASE + 0x400000 + SPMI_CH_OFFSET(ch) + 0x0C)
#define SPMI_CH_STATUS(ch)              (SPMI_BASE + 0x400000 + SPMI_CH_OFFSET(ch) + 0x10)
#define SPMI_CH_RDATA0(ch)              (SPMI_BASE + 0x600000 + SPMI_CH_OFFSET(ch) + 0x00)
#define SPMI_CH_RDATA1(ch)              (SPMI_BASE + 0x600000 + SPMI_CH_OFFSET(ch) + 0x04)

// SPMI commands
#define SPMI_CMD_EXT_REG_READ_LONG      0x00
#define SPMI_CMD_EXT_REG_WRITE_LONG     0x30

// SPMI STATUS bits
#define SPMI_STATUS_DONE                BIT0
#define SPMI_STATUS_FAILURE             BIT1
#define SPMI_STATUS_DENIED              BIT2
#define SPMI_STATUS_DROPPED             BIT3

// ---------------------------------------------------------------------------
// PMIC slave IDs
// ---------------------------------------------------------------------------
#define PM8994_SID0                     0   // PM8994 primary
#define PM8994_SID1                     1   // PM8994 interrupt
#define PMI8994_SID2                    2   // PMI8994 primary
#define PMI8994_SID3                    3   // PMI8994 interrupt

// ---------------------------------------------------------------------------
// PM8994 peripheral base addresses (SPMI byte-addressable)
// ---------------------------------------------------------------------------
// LDOs (output: e.g. LDO12 = display VDDIO 1.8V)
#define PM8994_LDO_BASE(n)              (0x4000 + ((n)-1) * 0x100)
#define PM8994_SMPS_BASE(n)             (0x1400 + ((n)-1) * 0x300)

// Key PMIC registers (offset within peripheral)
#define PMIC_EN_CTL                     0x046
#define PMIC_MODE_CTL                   0x045
#define PMIC_OUTPUT_VOLTAGE_LB          0x040
#define PMIC_OUTPUT_VOLTAGE_UB          0x041
#define PMIC_EN_CTL_ENABLE              BIT7

// PM8994 regulators used by GTS3L display
// LDO12: 1.8V VDDIO for display panel (NT35597)
// LDO28: 3.0V VDDA for display panel
// S5:    1.8V VDD_CX
// S7:    1.8V VDD_MX (memory)
#define PM8994_LDO12_BASE               PM8994_LDO_BASE(12)
#define PM8994_LDO28_BASE               PM8994_LDO_BASE(28)

// PMI8994 — charger and display bias
// PMI8994 IBB (inverting buck-boost) — negative rail for LCD
#define PMI8994_IBB_BASE                0xDC00
// PMI8994 LAB (LCD Anode Boost) — positive rail
#define PMI8994_LAB_BASE                0xDE00

// ---------------------------------------------------------------------------
// Helper: SPMI extended register read/write
// ---------------------------------------------------------------------------

STATIC UINTN  mSpmiChannel = 0;    // channel 0 for PM8994 SID0

STATIC
EFI_STATUS
SpmiWrite (
  IN UINT8   SlaveId,
  IN UINT16  RegAddr,
  IN UINT8   Data
  )
{
  UINT32  Cmd0, Cmd1, Status;
  UINTN   Timeout;

  // Build command: EXT_REG_WRITE_LONG, 1 byte
  Cmd0 = (SPMI_CMD_EXT_REG_WRITE_LONG << 27) |
         (SlaveId << 20) |
         ((RegAddr >> 8) << 4) |
         0;         // byte count - 1 = 0

  Cmd1 = ((RegAddr & 0xFF) << 8) | Data;

  MmioWrite32 (SPMI_CH_CMD1 (mSpmiChannel), Cmd1);
  MmioWrite32 (SPMI_CH_CMD0 (mSpmiChannel), Cmd0);

  Timeout = 1000;
  do {
    Status = MmioRead32 (SPMI_CH_STATUS (mSpmiChannel));
    if (Status & SPMI_STATUS_DONE) break;
    MicroSecondDelay (1);
  } while (--Timeout);

  if (!(Status & SPMI_STATUS_DONE) || (Status & (SPMI_STATUS_FAILURE | SPMI_STATUS_DENIED))) {
    DEBUG ((DEBUG_WARN, "SPMI: Write sid=%d reg=0x%04X failed: 0x%08X\n",
            SlaveId, RegAddr, Status));
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SpmiRead (
  IN  UINT8   SlaveId,
  IN  UINT16  RegAddr,
  OUT UINT8   *Data
  )
{
  UINT32  Cmd0, Status;
  UINTN   Timeout;

  Cmd0 = (SPMI_CMD_EXT_REG_READ_LONG << 27) |
         (SlaveId << 20) |
         ((RegAddr >> 8) << 4) |
         0;

  MmioWrite32 (SPMI_CH_CMD1 (mSpmiChannel), (RegAddr & 0xFF) << 8);
  MmioWrite32 (SPMI_CH_CMD0 (mSpmiChannel), Cmd0);

  Timeout = 1000;
  do {
    Status = MmioRead32 (SPMI_CH_STATUS (mSpmiChannel));
    if (Status & SPMI_STATUS_DONE) break;
    MicroSecondDelay (1);
  } while (--Timeout);

  if (!(Status & SPMI_STATUS_DONE) || (Status & (SPMI_STATUS_FAILURE | SPMI_STATUS_DENIED))) {
    return EFI_DEVICE_ERROR;
  }

  *Data = (UINT8)(MmioRead32 (SPMI_CH_RDATA0 (mSpmiChannel)) & 0xFF);
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// PMIC regulator helpers
// ---------------------------------------------------------------------------

STATIC
VOID
PmicRegulatorEnable (
  IN UINT8   Sid,
  IN UINT16  PeriphBase
  )
{
  SpmiWrite (Sid, PeriphBase + PMIC_EN_CTL, PMIC_EN_CTL_ENABLE);
}

STATIC
VOID
PmicLdoSetVoltage (
  IN UINT8   Sid,
  IN UINT16  PeriphBase,
  IN UINT32  VoltUv    // micro-volts
  )
{
  // Low-power LDO: voltage encoding = (mV - 375) / 12.5  (rounded)
  UINT32  Mv  = VoltUv / 1000;
  UINT8   Val = (UINT8)((Mv * 10 - 3750) / 125);

  SpmiWrite (Sid, PeriphBase + PMIC_OUTPUT_VOLTAGE_LB, Val);
  SpmiWrite (Sid, PeriphBase + PMIC_OUTPUT_VOLTAGE_UB, 0);
}

// ---------------------------------------------------------------------------
// Hardware initialization sequence
// ---------------------------------------------------------------------------

STATIC
VOID
PmicInitDisplayRails (
  VOID
  )
{
  // LDO12 → 1.8V (NT35597 VDDIO)
  PmicLdoSetVoltage (PM8994_SID0, PM8994_LDO12_BASE, 1800000);
  PmicRegulatorEnable (PM8994_SID0, PM8994_LDO12_BASE);

  // LDO28 → 3.0V (NT35597 VDDA)
  PmicLdoSetVoltage (PM8994_SID0, PM8994_LDO28_BASE, 3000000);
  PmicRegulatorEnable (PM8994_SID0, PM8994_LDO28_BASE);

  // PMI8994 LAB → +5.5V (panel positive bias, from qpnp_lab)
  SpmiWrite (PMI8994_SID2, PMI8994_LAB_BASE + PMIC_EN_CTL, PMIC_EN_CTL_ENABLE);
  // PMI8994 IBB → -5.5V (panel negative bias, from qpnp_ibb)
  SpmiWrite (PMI8994_SID2, PMI8994_IBB_BASE + PMIC_EN_CTL, PMIC_EN_CTL_ENABLE);

  DEBUG ((DEBUG_INFO, "PMIC: Display voltage rails enabled\n"));
}

STATIC
VOID
PmicReadChipId (
  VOID
  )
{
  UINT8  Rev1 = 0, Rev2 = 0, Rev3 = 0, Rev4 = 0;

  // PM8994 revision registers @ 0x0100
  SpmiRead (PM8994_SID0, 0x0100, &Rev1);
  SpmiRead (PM8994_SID0, 0x0101, &Rev2);
  SpmiRead (PM8994_SID0, 0x0102, &Rev3);
  SpmiRead (PM8994_SID0, 0x0103, &Rev4);

  DEBUG ((DEBUG_INFO, "PMIC: PM8994 rev %d.%d.%d.%d\n", Rev4, Rev3, Rev2, Rev1));

  SpmiRead (PMI8994_SID2, 0x0100, &Rev1);
  DEBUG ((DEBUG_INFO, "PMIC: PMI8994 rev %d\n", Rev1));
}

/**
  Entry point for SPMI/PMIC DXE driver.
**/
EFI_STATUS
EFIAPI
SpmiPmicDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "SpmiPmicDxe: SPMI arbiter @ 0x%08X\n", MSM8996_SPMI_BASE));

  PmicReadChipId ();
  PmicInitDisplayRails ();

  DEBUG ((DEBUG_INFO, "SpmiPmicDxe: PMIC init complete\n"));
  return EFI_SUCCESS;
}
