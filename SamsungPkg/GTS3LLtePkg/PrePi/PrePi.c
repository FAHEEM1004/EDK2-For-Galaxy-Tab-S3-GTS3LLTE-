/** @file
  Pre-PI entry point for Samsung Galaxy Tab S3 LTE (GTS3L LTE, SM-T825).

  This module is the very first UEFI code to run after XBL (eXtensible
  Bootloader) hands off control to the UEFI firmware image.

  XBL places the UEFI image at:
    Physical base: 0x80200000  ("UEFI FD" region, verified from xbl.elf)
    UEFI Stack:    0x80C00000  256 KB ("UEFI Stack" region)
    CPU Vectors:   0x80C40000  64 KB

  Register state on entry from XBL:
    x0 = FD base (0x80200000)
    x1 = FD size (0x00100000)
    x2 = stack base (0x80C00000)
    x3 = stack size (0x00040000)
    SP already set by XBL to top of UEFI Stack

  This module:
    1. Initialises BLSP2 UART1 (0x075B0000) for debug output
    2. Sets up the MMU with the XBL-verified platform memory map
    3. Enables I+D caches
    4. Builds the HOB list in HLOS1 (0x80D00000, 51 MB free)
    5. Hands off to DXE core (DxeLoadCore)

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <Library/ArmLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrePiLib.h>
#include <Library/SerialPortLib.h>

#include <Platform/GTS3LLte.h>

//
// External declaration: memory table is defined in PlatformMemoryMap.c
//
extern ARM_MEMORY_REGION_DESCRIPTOR  gGTS3LMemoryTable[];

/**
  Entry point called by XBL after loading the UEFI FD.

  @param  FdBase    Physical base of the firmware image (0x80200000).
  @param  FdSize    Size of the firmware image (0x00100000, 1 MB).
  @param  StackBase Bottom of UEFI Stack provided by XBL (0x80C00000).
  @param  StackSize Size of UEFI Stack (0x00040000, 256 KB).
**/
VOID
EFIAPI
PrePiMain (
  IN  UINTN  FdBase,
  IN  UINTN  FdSize,
  IN  UINTN  StackBase,
  IN  UINTN  StackSize
  )
{
  EFI_HOB_HANDOFF_INFO_TABLE  *HobList;
  EFI_STATUS                   Status;

  //
  // Initialise debug UART first so we get output immediately.
  // BLSP2 UART1 @ 0x075B0000, 115200 baud, 8-N-1.
  //
  SerialPortInitialize ();

  DEBUG ((DEBUG_INFO, "\n\n"));
  DEBUG ((DEBUG_INFO, "========================================\n"));
  DEBUG ((DEBUG_INFO, " Samsung Galaxy Tab S3 LTE (SM-T825)\n"));
  DEBUG ((DEBUG_INFO, " EDK2 UEFI Firmware — MSM8996 AArch64\n"));
  DEBUG ((DEBUG_INFO, "========================================\n"));
  DEBUG ((DEBUG_INFO, " FD   @ 0x%08lX  size 0x%08lX\n", FdBase, FdSize));
  DEBUG ((DEBUG_INFO, " Stack@ 0x%08lX  size 0x%08lX\n", StackBase, StackSize));
  DEBUG ((DEBUG_INFO, " UART : 0x%08X  115200 8N1\n",    MSM8996_DEBUG_UART_BASE));
  DEBUG ((DEBUG_INFO, " GIC  : GICD=0x%08X GICR=0x%08X\n",
          MSM8996_GIC_DIST_BASE, MSM8996_GIC_REDIST_BASE));
  DEBUG ((DEBUG_INFO, " Timer: %u Hz\n", MSM8996_TIMER_FREQ_HZ));
  DEBUG ((DEBUG_INFO, "========================================\n"));

  //
  // Validate FD base matches XBL expectation.
  //
  if (FdBase != MSM8996_FD_BASE) {
    DEBUG ((DEBUG_WARN,
            "PrePi: FD base 0x%lX != expected 0x%lX — using PCD value\n",
            FdBase, (UINTN)MSM8996_FD_BASE));
    FdBase = MSM8996_FD_BASE;
    FdSize = MSM8996_FD_SIZE;
  }

  //
  // Configure MMU with the XBL-verified memory map.
  // All DRAM regions and SoC MMIO ranges are mapped.
  //
  Status = ArmConfigureMmu (
             gGTS3LMemoryTable,
             NULL,
             NULL
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "PrePi: ArmConfigureMmu FAILED: %r\n", Status));
    CpuDeadLoop ();
  }
  DEBUG ((DEBUG_INFO, "PrePi: MMU configured\n"));

  //
  // Enable I-cache and D-cache.
  //
  ArmEnableInstructionCache ();
  ArmEnableDataCache ();
  DEBUG ((DEBUG_INFO, "PrePi: Caches enabled\n"));

  //
  // Free DRAM for DXE:
  //   HLOS1: 0x80D00000 – 0x83FFFFFF (51 MB)  ← immediately above FD+stack area
  //   HLOS2: 0x84000000 – 0x857FFFFF (24 MB)
  //
  // We use HLOS1 as the primary HOB/DXE pool.
  //
  UINTN  MemBase = MSM8996_UEFI_RAM_BASE;
  UINTN  MemSize = MSM8996_UEFI_RAM_SIZE;

  DEBUG ((DEBUG_INFO, "PrePi: DXE RAM 0x%08lX – 0x%08lX (%lu MB)\n",
          MemBase, MemBase + MemSize - 1, MemSize >> 20));

  //
  // Build the initial HOB list.
  //
  HobList = HobConstructor (
              (VOID *)MemBase,
              MemSize,
              (VOID *)MemBase,
              (VOID *)(MemBase + MemSize)
              );
  PrePeiSetHobList (HobList);

  //
  // Publish the main free DRAM HOB (HLOS1+2 — free for DXE allocations).
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    EFI_RESOURCE_ATTRIBUTE_PRESENT         |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED     |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE     |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE,
    MemBase,
    MemSize
    );

  //
  // Firmware volume HOBs — DXE core uses these to find drivers.
  //
  BuildFvHob (FdBase, FdSize);

  //
  // CPU HOB — 36-bit physical address space (MSM8996 Kryo).
  //
  BuildCpuHob (36, 36);

  //
  // Stack HOB — inform DXE where the boot stack lives.
  //
  BuildStackHob (StackBase, StackSize);

  //
  // SMEM HOB — reserve shared memory region so DXE doesn't allocate into it.
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_PRESENT,
    MSM8996_SMEM_BASE,
    MSM8996_SMEM_SIZE
    );

  //
  // Framebuffer HOB — reserve display region (write-through, DXE must not cache it).
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE,
    MSM8996_DISPLAY_FB_BASE,
    MSM8996_DISPLAY_FB_SIZE
    );

  //
  // TrustZone / HYP / PIL reservation HOBs — DXE must not touch these.
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_PRESENT,
    MSM8996_HYP_BASE,
    MSM8996_HYP_SIZE
    );
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_PRESENT,
    MSM8996_TZ_BASE,
    MSM8996_TZ_SIZE + 0x00E00000   // TZ + TZ Apps + Stargate
    );
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_RESERVED,
    EFI_RESOURCE_ATTRIBUTE_PRESENT,
    MSM8996_PIL_BASE,
    MSM8996_PIL_SIZE
    );

  DEBUG ((DEBUG_INFO, "PrePi: HOB list built — handing off to DXE core\n"));

  //
  // Transfer to DXE core — this function does not return.
  //
  DxeLoadCore (0);

  // Should never be reached
  CpuDeadLoop ();
}

/**
  Assembly-level entry point from XBL reset vector trampoline.
  Defined in PrePiEntry.S — sets up SP, calls PrePiMain with
  (FdBase, FdSize, StackBase, StackSize) in x0–x3.
**/
VOID
EFIAPI
_ModuleEntryPoint (
  VOID
  );
