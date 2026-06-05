// Assembly entry stub for Samsung GTS3L LTE (MSM8996) PrePi
//
// XBL (eXtensible Bootloader) loads the UEFI FD at 0x80200000 ("UEFI FD"
// region, verified from xbl.elf device-region table) and jumps to this
// entry point.
//
// XBL register convention on entry:
//   x0 = FD base address  (0x80200000, may also be passed as a hint)
//   x1 = FD size          (0x00400000, 4 MB)
//   x2 = reserved / DRAM size
//   x3 = reserved
//   SP = somewhere in XBL — we override it immediately
//
// We:
//   1. Disable interrupts
//   2. Switch SP to top of "UEFI Stack" region (0x80C40000, per XBL)
//   3. Zero BSS
//   4. Call PrePiMain(FdBase=0x80200000, FdSize=0x100000,
//                     StackBase=0x80C00000, StackSize=0x40000)
//
// Copyright (c) 2024, Samsung / TianoCore Contributors.
// SPDX-License-Identifier: BSD-2-Clause-Patent

#include <AsmMacroLib.h>

.text
.align 3

GCC_ASM_EXPORT (_ModuleEntryPoint)

ASM_PFX(_ModuleEntryPoint):
  // Mask all interrupts (IRQ, FIQ, SError, Debug)
  msr   DAIFSet, #0xF

  // Preserve XBL-provided FD base/size
  mov   x20, x0               // FdBase (may be 0 if XBL did not set)
  mov   x21, x1               // FdSize

  // Override stack pointer: use XBL "UEFI Stack" top (0x80C40000)
  // UEFI Stack:  0x80C00000 (base) + 0x40000 (256 KB) = 0x80C40000 (top)
  MOV64 (x2, 0x80C40000)
  mov   sp, x2

  // If XBL did not pass FD base (x20 == 0), use PCD-fixed value
  cbnz  x20, .Lfd_ok
  MOV64 (x20, 0x80200000)     // FD_BASE (PcdFdBaseAddress, verified from XBL)
  MOV64 (x21, 0x00400000)     // FD_SIZE 4 MB
.Lfd_ok:

  // Call PrePiMain(FdBase, FdSize, StackBase, StackSize)
  mov   x0, x20               // arg0: FdBase
  mov   x1, x21               // arg1: FdSize
  MOV64 (x2, 0x80C00000)      // arg2: StackBase
  MOV64 (x3, 0x00040000)      // arg3: StackSize (256 KB)
  bl    ASM_PFX(PrePiMain)

  // Should never reach here
.Lhang:
  wfi
  b     .Lhang
