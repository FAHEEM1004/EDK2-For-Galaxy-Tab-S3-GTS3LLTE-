/** @file
  Platform initialisation DXE driver for Samsung Galaxy Tab S3 LTE.

  Responsibilities:
    - Publish the SMBIOS Type 0 / 1 / 4 / 17 tables with correct device info
    - Install the reset/shutdown handler (PSHOLD)
    - Notify when hardware init is done

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/ResetNotification.h>
#include <IndustryStandard/SmBios.h>
#include <Protocol/Smbios.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// PSHOLD-based reset / shutdown
// ---------------------------------------------------------------------------

/**
  Trigger a hard reset by de-asserting PSHOLD.

  Writing 0 to the PSHOLD register tells the PMIC to cut power immediately,
  which causes the SoC to reboot on the next power-button press (or auto
  restart if configured).
**/
STATIC VOID
MsmPsHoldReset (
  VOID
  )
{
  MmioWrite32 ((UINTN)MSM8996_PSHOLD_BASE, 0);
  // Should not return — spin if it does
  for (;;) {
    ArmCallWFI ();
  }
}

/**
  ResetSystem runtime service handler.

  @param  ResetType   Type of reset (cold, warm, shutdown, platform-specific).
  @param  ResetStatus Status code propagated to the next boot.
  @param  DataSize    Size of ResetData.
  @param  ResetData   Optional data.
**/
VOID
EFIAPI
GTS3LResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetShutdown:
      DEBUG ((DEBUG_INFO, "GTS3L: Shutdown via PSHOLD\n"));
      MsmPsHoldReset ();
      break;

    case EfiResetCold:
    case EfiResetWarm:
    default:
      DEBUG ((DEBUG_INFO, "GTS3L: Reset via PSHOLD\n"));
      MsmPsHoldReset ();
      break;
  }
}

// ---------------------------------------------------------------------------
// SMBIOS helper — minimal tables for OS identification
// ---------------------------------------------------------------------------

#pragma pack(1)
typedef struct {
  SMBIOS_TABLE_TYPE0  Base;
  CHAR8               Strings[64];
} SMBIOS_TYPE0_RECORD;

typedef struct {
  SMBIOS_TABLE_TYPE1  Base;
  CHAR8               Strings[128];
} SMBIOS_TYPE1_RECORD;

typedef struct {
  SMBIOS_TABLE_TYPE4  Base;
  CHAR8               Strings[64];
} SMBIOS_TYPE4_RECORD;

typedef struct {
  SMBIOS_TABLE_TYPE17  Base;
  CHAR8                Strings[64];
} SMBIOS_TYPE17_RECORD;
#pragma pack()

STATIC EFI_STATUS
InstallSmbios (
  VOID
  )
{
  EFI_STATUS            Status;
  EFI_SMBIOS_PROTOCOL  *Smbios;
  EFI_SMBIOS_HANDLE     Handle;

  Status = gBS->LocateProtocol (&gEfiSmbiosProtocolGuid, NULL, (VOID **)&Smbios);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "GTS3L: SMBIOS protocol not found: %r\n", Status));
    return Status;
  }

  // --- Type 0: BIOS Information ---
  STATIC SMBIOS_TYPE0_RECORD  Type0 = {
    .Base = {
      .Hdr = {
        .Type   = SMBIOS_TYPE_BIOS_INFORMATION,
        .Length = sizeof (SMBIOS_TABLE_TYPE0),
        .Handle = 0,
      },
      .Vendor              = 1,
      .BiosVersion         = 2,
      .BiosSegment         = 0,
      .BiosReleaseDate     = 3,
      .BiosSize            = 0,
      .BiosCharacteristics = {0},
      .BIOSCharacteristicsExtensionBytes = {0},
      .SystemBiosMajorRelease  = 0,
      .SystemBiosMinorRelease  = 1,
      .EmbeddedControllerFirmwareMajorRelease = 0xFF,
      .EmbeddedControllerFirmwareMinorRelease = 0xFF,
    },
    // Strings: 1=Vendor 2=Version 3=Date, double-NUL terminates
    .Strings = "TianoCore EDK2\0" "GTS3L-UEFI-1.0\0" "2024-01-01\0\0",
  };

  // --- Type 1: System Information ---
  STATIC SMBIOS_TYPE1_RECORD  Type1 = {
    .Base = {
      .Hdr = {
        .Type   = SMBIOS_TYPE_SYSTEM_INFORMATION,
        .Length = sizeof (SMBIOS_TABLE_TYPE1),
        .Handle = 0,
      },
      .Manufacturer = 1,
      .ProductName  = 2,
      .Version      = 3,
      .SerialNumber = 4,
      .WakeUpType   = SystemWakeupTypePowerSwitch,
      .SKUNumber    = 5,
      .Family       = 6,
    },
    .Strings = "Samsung\0" "Galaxy Tab S3 LTE\0" "SM-T825\0"
               "Unknown\0" "GTS3LLTE\0" "Galaxy Tab\0\0",
  };

  // --- Type 4: Processor Information ---
  STATIC SMBIOS_TYPE4_RECORD  Type4 = {
    .Base = {
      .Hdr = {
        .Type   = SMBIOS_TYPE_PROCESSOR_INFORMATION,
        .Length = sizeof (SMBIOS_TABLE_TYPE4),
        .Handle = 0,
      },
      .Socket          = 1,
      .ProcessorType   = CentralProcessor,
      .ProcessorFamily = ProcessorFamilyIndicatorFamily2,
      .ProcessorManufacturer = 2,
      .ProcessorVersion = 3,
      .MaxSpeed        = 2150,   // MHz (Gold cluster peak)
      .CurrentSpeed    = 2150,
      .Status          = 0x41,   // CPU Enabled, Populated
      .ProcessorUpgrade = ProcessorUpgradeUnknown,
      .CoreCount       = 4,
      .EnabledCoreCount = 4,
      .ThreadCount     = 4,
      .ProcessorFamily2 = ProcessorFamilyARMv8,
    },
    .Strings = "U6\0" "Qualcomm\0" "Snapdragon 820 (MSM8996)\0\0",
  };

  // --- Type 17: Memory Device ---
  STATIC SMBIOS_TYPE17_RECORD  Type17 = {
    .Base = {
      .Hdr = {
        .Type   = SMBIOS_TYPE_MEMORY_DEVICE,
        .Length = sizeof (SMBIOS_TABLE_TYPE17),
        .Handle = 0,
      },
      .MemoryType    = MemoryTypeLpddr4,
      .Size          = 4096,    // MB
      .FormFactor    = MemoryFormFactorDie,
      .Speed         = 1866,    // MT/s
      .Manufacturer  = 1,
      .DataWidth     = 64,
      .TotalWidth    = 64,
    },
    .Strings = "Samsung\0\0",
  };

  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)&Type0);
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)&Type1);
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)&Type4);
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  Smbios->Add (Smbios, NULL, &Handle, (EFI_SMBIOS_TABLE_HEADER *)&Type17);

  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver entry point
// ---------------------------------------------------------------------------

/**
  Platform DXE driver entry point.

  @param  ImageHandle  Image handle for this driver.
  @param  SystemTable  Pointer to the EFI System Table.
  @return EFI_SUCCESS on success.
**/
EFI_STATUS
EFIAPI
PlatformDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "GTS3L: PlatformDxe initialising\n"));
  DEBUG ((DEBUG_INFO, "GTS3L: SoC   = Qualcomm MSM8996 (Snapdragon 820)\n"));
  DEBUG ((DEBUG_INFO, "GTS3L: UART  = 0x%08x (msm-lsuart-v14)\n", MSM8996_DEBUG_UART_BASE));
  DEBUG ((DEBUG_INFO, "GTS3L: GICd  = 0x%08x\n", MSM8996_GIC_DIST_BASE));
  DEBUG ((DEBUG_INFO, "GTS3L: GICr  = 0x%08x\n", MSM8996_GIC_REDIST_BASE));
  DEBUG ((DEBUG_INFO, "GTS3L: Timer = %u Hz\n",   MSM8996_TIMER_FREQ_HZ));

  // Install SMBIOS tables
  InstallSmbios ();

  // Override the ResetSystem runtime service with our PSHOLD implementation
  gRT->ResetSystem = GTS3LResetSystem;

  DEBUG ((DEBUG_INFO, "GTS3L: PlatformDxe done\n"));
  return EFI_SUCCESS;
}
