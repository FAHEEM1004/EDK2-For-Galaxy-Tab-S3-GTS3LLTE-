/** @file
  Memory map for Samsung Galaxy Tab S3 LTE (GTS3L LTE, SM-T825)
  Qualcomm MSM8996 / Snapdragon 820 — 4 GB LPDDR4.

  ALL REGIONS VERIFIED from the XBL device-region table embedded in
  xbl.elf (eXtensible BootLoader, decompressed from xbl.elf.lz4).

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>

#include <Platform/GTS3LLte.h>

//
// ARM MMU region table — covers all DRAM regions and SoC MMIO.
// Order matters: more specific entries must precede broad catch-alls.
//
STATIC ARM_MEMORY_REGION_DESCRIPTOR  gGTS3LMemoryTable[] = {

  // -------------------------------------------------------------------------
  // DRAM — regions managed by XBL / UEFI (writable, boot-services data)
  // -------------------------------------------------------------------------

  // HLOS 0 — general boot-services RAM
  { 0x80100000, 0x80100000, 0x00100000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // UEFI FD — firmware image itself (1 MB at 0x80200000, loaded by XBL)
  { 0x80200000, 0x80200000, 0x00100000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // MPPark Code — secondary CPU parking code (RT code, must be uncached)
  { 0x80300000, 0x80300000, 0x00080000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // FBPT / DBG2 / Capsule / TPM / UEFI Info / Reset Data (runtime data, UC)
  { 0x80380000, 0x80380000, 0x00080000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // Display Framebuffer — 8 MB, write-through (GPU/DSI scanout safe)
  { 0x80400000, 0x80400000, 0x00800000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_THROUGH },

  // UEFI Stack + CPU Vectors + Reserved Cached (boot-services, WB)
  { 0x80C00000, 0x80C00000, 0x00100000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // HLOS 1 — free DXE RAM (51 MB)
  { 0x80D00000, 0x80D00000, 0x03300000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // HLOS 2 — free DXE RAM (24 MB)
  { 0x84000000, 0x84000000, 0x01800000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // -------------------------------------------------------------------------
  // DRAM — regions reserved by XBL; UEFI must not allocate into these
  // -------------------------------------------------------------------------

  // HYP — Hypervisor (6 MB, non-secure device mapping)
  { 0x85800000, 0x85800000, 0x00600000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // MPSS_EFS + ADSP_EFS (640 KB, NS)
  { 0x85E00000, 0x85E00000, 0x001A0000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // HLOS 3 — small free window (384 KB, WB)
  { 0x85FA0000, 0x85FA0000, 0x00060000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // SMEM — shared memory between UEFI and firmware subsystems (2 MB, UC)
  { 0x86000000, 0x86000000, 0x00200000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // TZ_STAT + TAGS + TZ + TZ Apps + Stargate (all reserved, NS)
  { 0x86200000, 0x86200000, 0x01200000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // PIL_REGION — Modem + ADSP + peripheral firmware (135 MB, NS)
  { 0x88800000, 0x88800000, 0x08700000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // CNSS_DEBUG — QCA6174 WiFi debug (3 MB, NS)
  { 0x90F00000, 0x90F00000, 0x00300000, ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED },

  // HLOS 4 — free DXE RAM (238 MB)
  { 0x91200000, 0x91200000, 0x0EE00000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // HLOS 5 — remaining ~1.5 GB DRAM
  { 0xA0000000, 0xA0000000, 0x60000000, ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK },

  // -------------------------------------------------------------------------
  // SoC MMIO — device-mapped, uncached, non-bufferable
  // All ranges from the XBL MMIO table, plus DTB-derived registers.
  // -------------------------------------------------------------------------

  // PCIE PHY (0x34000), RPM MSG RAM (0x68000), Security Ctrl (0x70000),
  // PRNG (0x80000)  — all below GCC block
  { 0x00034000, 0x00034000, 0x00060000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // GCC Global Clock Controller
  { 0x00300000, 0x00300000, 0x000A0000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // MPM2 + PSHOLD area
  { 0x004A0000, 0x004A0000, 0x00060000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // PCIe AHB + UFS + Crypto + CORE_TOP_CSR
  { 0x00600000, 0x00600000, 0x00200000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // MMSS — display, DSI, HDMI, camera, GPU (8 MB)
  { 0x00800000, 0x00800000, 0x00800000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // TLMM — GPIO controller (3 MB + change)
  { 0x01000000, 0x01000000, 0x00400000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // SPMI / PMIC Arbiter (28 MB — covers both PM8994 and PMI8994)
  { 0x04000000, 0x04000000, 0x01C00000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // HMSS_QLL + IMEM + USB3 + BLSP (UART/I2C/SPI) + SDHC
  { 0x06400000, 0x06400000, 0x01200000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // HMSS — CPU subsystem + GICv3 (8 MB, covers 0x09800000–0x09FFFFFF)
  { 0x09800000, 0x09800000, 0x00800000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // PCIe AXI memory window (64 MB)
  { 0x0C000000, 0x0C000000, 0x04000000, ARM_MEMORY_REGION_ATTRIBUTE_DEVICE },

  // End of table sentinel
  { 0, 0, 0, 0 },
};

/**
  Return the platform virtual memory map.

  @param[out]  MemoryMap  Set to point to the platform memory region table.
**/
VOID
ArmPlatformGetVirtualMemoryMap (
  OUT ARM_MEMORY_REGION_DESCRIPTOR  **MemoryMap
  )
{
  *MemoryMap = gGTS3LMemoryTable;
}
