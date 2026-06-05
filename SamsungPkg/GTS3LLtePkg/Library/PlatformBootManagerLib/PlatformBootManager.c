/** @file
  Platform Boot Manager for Samsung Galaxy Tab S3 LTE (GTS3L LTE).

  Boot order:
    1. UFS internal storage (eMMC-style block device via UFS)
    2. USB mass storage
    3. UEFI Shell (fallback)

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Guid/GlobalVariable.h>

/**
  Called before the UEFI Boot Manager processes boot options.

  We use this to connect all controllers so that disk/USB devices appear
  before the boot option list is evaluated.

  @param  BootMode   Current boot mode.
**/
VOID
EFIAPI
PlatformBootManagerBeforeConsole (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_BOOT_MANAGER_LOAD_OPTION  BootOption;
  EFI_DEVICE_PATH_PROTOCOL      *ShellDevicePath;

  DEBUG ((DEBUG_INFO, "GTS3L: PlatformBootManagerBeforeConsole\n"));

  // Connect all controllers (recursive) to discover block devices, USB, etc.
  EfiBootManagerConnectAll ();

  // Enumerate all boot options from connected devices
  EfiBootManagerRefreshAllBootOption ();

  // Add the UEFI Shell as a boot option if it is not already present.
  ShellDevicePath = ConvertTextToDevicePath (L"Fv(*)\\EFI\\Shell\\Shell.efi");
  if (ShellDevicePath != NULL) {
    Status = EfiBootManagerInitializeLoadOption (
               &BootOption,
               LoadOptionNumberUnassigned,
               LoadOptionTypeBoot,
               LOAD_OPTION_ACTIVE,
               L"UEFI Shell",
               ShellDevicePath,
               NULL,
               0
               );
    if (!EFI_ERROR (Status)) {
      EfiBootManagerAddLoadOptionVariable (&BootOption, MAX_UINTN);
      EfiBootManagerFreeLoadOption (&BootOption);
    }
    FreePool (ShellDevicePath);
  }
}

/**
  Called after the UEFI Boot Manager processes boot options.

  Connects GOP, sets up the console, and prints a banner.

  @param  BootMode   Current boot mode.
**/
VOID
EFIAPI
PlatformBootManagerAfterConsole (
  VOID
  )
{
  EFI_STATUS                     Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  DEBUG ((DEBUG_INFO, "GTS3L: PlatformBootManagerAfterConsole\n"));

  // Attempt to locate GOP (display)
  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&Gop
                  );
  if (!EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "GTS3L: GOP found — %ux%u mode %u\n",
      Gop->Mode->Info->HorizontalResolution,
      Gop->Mode->Info->VerticalResolution,
      Gop->Mode->Mode
      ));
  }

  // Print boot banner on serial and console
  Print (L"\n");
  Print (L"============================================================\n");
  Print (L"  Samsung Galaxy Tab S3 LTE (GTS3L LTE / SM-T825)\n");
  Print (L"  SoC  : Qualcomm MSM8996 (Snapdragon 820)\n");
  Print (L"  CPU  : 4x Qualcomm Kryo (2+2 clusters)\n");
  Print (L"  RAM  : 4 GB LPDDR4\n");
  Print (L"  UFS  : JEDEC UFS 1.1\n");
  Print (L"  EDK2 UEFI Firmware\n");
  Print (L"============================================================\n");
  Print (L"\n");
}

/**
  Called when no boot options succeed and the Boot Manager gives up.

  We drop to the UEFI Shell.
**/
VOID
EFIAPI
PlatformBootManagerUnableToBoot (
  VOID
  )
{
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  UINTN                         BootOptionCount;
  UINTN                         Index;

  DEBUG ((DEBUG_WARN, "GTS3L: No bootable device found — launching Shell\n"));

  // Find the UEFI Shell boot option and run it
  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  for (Index = 0; Index < BootOptionCount; Index++) {
    if (StrCmp (BootOptions[Index].Description, L"UEFI Shell") == 0) {
      EfiBootManagerBoot (&BootOptions[Index]);
      if (EFI_ERROR (BootOptions[Index].Status)) {
        DEBUG ((DEBUG_ERROR, "GTS3L: Shell launch failed: %r\n", BootOptions[Index].Status));
      }
      break;
    }
  }

  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}

/**
  Register platform default console devices (serial + GOP).
**/
VOID
EFIAPI
PlatformRegisterConsoles (
  VOID
  )
{
  // Serial (debug UART) is registered via ConIn/ConOut PCDs in the DSC.
  // This function is a placeholder for additional console registration
  // (e.g., USB keyboard) that may be added later.
}

/**
  Called once per second while the boot manager waits for its timeout.

  @param  TimeoutRemain  Remaining timeout in seconds.
**/
VOID
EFIAPI
PlatformBootManagerWaitCallback (
  UINT16  TimeoutRemain
  )
{
  DEBUG ((DEBUG_INFO, "GTS3L: Boot manager timeout: %u\n", TimeoutRemain));
}
