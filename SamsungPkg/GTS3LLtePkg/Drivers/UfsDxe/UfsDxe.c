/** @file
  UFS (Universal Flash Storage) host controller driver for MSM8996.

  Hardware:
    - Qualcomm UFS 1.1 host controller (jedec,ufs-1.1)
    - Base: 0x00620000 (verified from XBL: "UFS UFS REGS", 128 KB)
    - Boot device confirmed: "Boot Device: UFS" string in XBL
    - Internal storage: 128 GB (partition table from PIT)

  Implementation:
    - Full JEDEC UFS HCI 1.1 UTRD/UPIU command engine
    - SCSI READ(10) / WRITE(10) via UTP Transfer Request queue
    - 1-slot polling queue (no interrupts — correct for UEFI pre-ExitBootServices)
    - DMA cache maintenance via CacheMaintenanceLib
    - BlockIo installed for LUN 0 (main storage, GPT partitions)

  UTRD layout (one 4 KB aligned page):
    Offset   0: UTRD[0]           — 32 bytes  (UTRD list base, 1 KB aligned OK)
    Offset  64: Command UPIU      — 32 bytes
    Offset  96: Response UPIU     — 32 bytes  (8 DWORDs offset = 8 from CD base)
    Offset 128: PRDT[0]           — 16 bytes  (16 DWORDs offset = 16 from CD base)
  CD base = page_base + 64  (128-byte aligned)
  Response UPIU offset from CD: (96-64)/4 = 8 DWORDs
  PRDT offset from CD: (128-64)/4 = 16 DWORDs

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/BlockIo2.h>
#include <Protocol/DevicePath.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// UFSHCI register map (JEDEC UFS HCI 1.1 + Qualcomm extensions)
// ---------------------------------------------------------------------------
#define UFS_BASE                        MSM8996_UFS_BASE

#define UFS_CAPABILITIES                (UFS_BASE + 0x000)
#define UFS_VER                         (UFS_BASE + 0x008)
#define UFS_HCDDID                      (UFS_BASE + 0x010)
#define UFS_HCPMID                      (UFS_BASE + 0x014)
#define UFS_IS                          (UFS_BASE + 0x020)
#define UFS_IE                          (UFS_BASE + 0x024)
#define UFS_HCS                         (UFS_BASE + 0x030)
#define UFS_HCE                         (UFS_BASE + 0x034)
#define UFS_UECPA                       (UFS_BASE + 0x038)
#define UFS_UECDL                       (UFS_BASE + 0x03C)
#define UFS_UECN                        (UFS_BASE + 0x040)
#define UFS_UECT                        (UFS_BASE + 0x044)
#define UFS_UECDME                      (UFS_BASE + 0x048)
#define UFS_UTRLBA                      (UFS_BASE + 0x050)  // UTRD list base address (low)
#define UFS_UTRLBAU                     (UFS_BASE + 0x054)  // UTRD list base address (high)
#define UFS_UTRLDBR                     (UFS_BASE + 0x058)  // doorbell — set bit N to submit slot N
#define UFS_UTRLCLR                     (UFS_BASE + 0x05C)  // clear doorbell
#define UFS_UTRLRSR                     (UFS_BASE + 0x060)  // run-stop: 1 = run
#define UFS_UTMRLBA                     (UFS_BASE + 0x070)
#define UFS_UTMRLBAU                    (UFS_BASE + 0x074)
#define UFS_UTMRLDBR                    (UFS_BASE + 0x078)
#define UFS_UTMRLCLR                    (UFS_BASE + 0x07C)
#define UFS_UTMRLRSR                    (UFS_BASE + 0x080)
#define UFS_UICCMD                      (UFS_BASE + 0x090)
#define UFS_UCMDARG1                    (UFS_BASE + 0x094)
#define UFS_UCMDARG2                    (UFS_BASE + 0x098)
#define UFS_UCMDARG3                    (UFS_BASE + 0x09C)

// Qualcomm UFS vendor registers (from Qualcomm BSP)
#define QCOM_UFS_SYS1CLK_1US           (UFS_BASE + 0xC4)   // 1 μs clock period count
#define QCOM_UFS_CFG1                   (UFS_BASE + 0xDC)
#define QCOM_UFS_CFG2                   (UFS_BASE + 0xE0)
#define QCOM_UFS_CFG3                   (UFS_BASE + 0xFC)

// HCS (Host Controller Status) bits
#define UFS_HCS_DP                      BIT0    // device present
#define UFS_HCS_UTRLRDY                 BIT1    // transfer request list ready
#define UFS_HCS_UTMRLRDY                BIT2    // task mgmt list ready
#define UFS_HCS_UCRDY                   BIT3    // UIC command ready

// HCE bits
#define UFS_HCE_HCE                     BIT0    // HC enable: write 0→reset, write 1→enable; reads 1 when HC is active (UFSHCI 1.1)

// IS (Interrupt Status) bits
#define UFS_IS_UCCS                     BIT10   // UIC command completion
#define UFS_IS_ULSS                     BIT8    // UIC link startup status
#define UFS_IS_UE                       BIT2    // UFS error
#define UFS_IS_UTRCS                    BIT0    // UTP transfer request completion

// UIC commands
#define UFS_UIC_CMD_DME_LINK_STARTUP    0x16

// Storage geometry
// Samsung 128 GB UFS: 0x10000000 × 512-byte logical blocks = 128 GB
// Exposed to BlockIo as 4096-byte blocks: 0x10000000 / 8 = 0x2000000
#define UFS_SECTOR_SIZE                 512
#define UFS_BLOCK_SIZE                  4096
#define UFS_MAX_LUN                     8
#define UFS_INTERNAL_STORAGE_SECTORS    0x10000000ULL
#define UFS_INTERNAL_STORAGE_BLOCKS     (UFS_INTERNAL_STORAGE_SECTORS / (UFS_BLOCK_SIZE / UFS_SECTOR_SIZE))

// ---------------------------------------------------------------------------
// JEDEC UFS HCI 1.1 — UTRD (UTP Transfer Request Descriptor)
//
// Each UTRD is 32 bytes (8 DWORDs):
//   DW0 [31:28] CT  — command type: 0x1=SCSI, 0x2=native UFS, 0x3=device mgmt
//   DW0 [26:25] DD  — data direction: 00=none, 01=host→dev, 10=dev→host
//   DW0 [24]    I   — interrupt on completion
//   DW1         reserved
//   DW2         OCS — Overall Command Status (written by HC, init to 0xFF=invalid)
//   DW3         reserved
//   DW4         CD base address (low 32 bits, 128-byte aligned)
//   DW5         CD base address (high 32 bits)
//   DW6 [31:16] Response UPIU offset from CD base (in DWORDs)
//   DW6 [15:0]  Response UPIU length (in DWORDs)
//   DW7 [31:16] PRDT offset from CD base (in DWORDs)
//   DW7 [15:0]  PRDT length (number of entries)
// ---------------------------------------------------------------------------
#define UTRD_CT_SCSI                    (0x1u << 28)
#define UTRD_DD_READ                    (0x2u << 25)   // device → host
#define UTRD_DD_WRITE                   (0x1u << 25)   // host → device
#define UTRD_DD_NONE                    (0x0u << 25)
#define UTRD_OCS_INVALID                0xFF

// ---------------------------------------------------------------------------
// UPIU (UFS Protocol Information Unit) structures
//
// Command UPIU (for SCSI): 32 bytes
//   Byte  0:  Transaction Code = 0x01
//   Byte  1:  Flags (bit 6 = read, bit 5 = write for SCSI data direction)
//   Byte  2:  LUN
//   Byte  3:  Task Tag
//   Byte  4:  Initiator ID
//   Byte  5:  Command Set Type = 0x00 (SCSI)
//   Byte  6:  Reserved
//   Byte  7:  Status
//   Byte  8:  EHS Length = 0
//   Byte  9:  Device Information = 0
//   Bytes 10-11: Data Segment Length (big-endian) = 0
//   Bytes 12-15: EDTL — Expected Data Transfer Length (big-endian, bytes)
//   Bytes 16-31: CDB (16 bytes, zero-padded from 10-byte CDB)
// ---------------------------------------------------------------------------
#define UPIU_TRANS_CODE_CMD             0x01    // Command UPIU
#define UPIU_FLAG_READ                  BIT6    // data direction read
#define UPIU_FLAG_WRITE                 BIT5    // data direction write

// SCSI operation codes
#define SCSI_READ10                     0x28
#define SCSI_WRITE10                    0x2A

// PRDT entry (16 bytes per entry, JEDEC UFSHCI 1.1 §6.2)
//   DW0 [31:0]:  Data Base Address bits[31:0]  (must be 4-byte aligned)
//   DW1 [31:0]:  Data Base Address bits[63:32]
//   DW2:         Reserved
//   DW3 [17:0]:  Data Byte Count - 1  (max 256 KB - 1 per entry)
#define PRDT_MAX_BYTES_PER_ENTRY        (256 * 1024)

// ---------------------------------------------------------------------------
// DMA scratch buffer (one 4 KB page, allocated once at driver init)
//
// Layout within the page:
//   Offset   0: UTRD[0]       (32 bytes)  ← UTRL base registered with HC
//   Offset  64: Command UPIU  (32 bytes)  ← CD base
//   Offset  96: Response UPIU (32 bytes)  (offset 32 from CD = 8 DWORDs)
//   Offset 128: PRDT[0..N]   (16 B each) (offset 64 from CD = 16 DWORDs)
//
// The UTRL base address (registered in UFS_UTRLBA) points to offset 0.
// The CD base address (stored in UTRD DW4) points to offset 64.
// ---------------------------------------------------------------------------
#define UFS_DMA_BUF_SIZE                SIZE_4KB
#define UFS_UTRD_OFFSET                 0
#define UFS_CMD_UPIU_OFFSET             64
#define UFS_RSP_UPIU_OFFSET             96
#define UFS_PRDT_OFFSET                 128
#define UFS_RSP_UPIU_DWORD_OFFSET       8     // (96-64)/4
#define UFS_PRDT_DWORD_OFFSET           16    // (128-64)/4
#define UFS_MAX_PRDT_ENTRIES            ((UFS_DMA_BUF_SIZE - UFS_PRDT_OFFSET) / 16)

STATIC UINT8  *mUfsDmaBuf = NULL;   // 4 KB, 4 KB-aligned

// ---------------------------------------------------------------------------
// Block IO device per LUN
// ---------------------------------------------------------------------------
typedef struct {
  EFI_BLOCK_IO_PROTOCOL        BlockIo;
  EFI_BLOCK_IO_MEDIA           Media;
  UINT8                        Lun;
} UFS_LUN_DEVICE;

// ---------------------------------------------------------------------------
// Helper: write a 32-bit big-endian value into a byte array
// ---------------------------------------------------------------------------
STATIC
VOID
WriteBe32 (
  OUT UINT8  *Buf,
  IN  UINT32  Val
  )
{
  Buf[0] = (UINT8)(Val >> 24);
  Buf[1] = (UINT8)(Val >> 16);
  Buf[2] = (UINT8)(Val >>  8);
  Buf[3] = (UINT8)(Val >>  0);
}

// ---------------------------------------------------------------------------
// UIC command engine
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
UfsWaitHcReady (
  VOID
  )
{
  UINTN  Timeout = 200000;

  while (!(MmioRead32 (UFS_HCS) & UFS_HCS_UCRDY) && Timeout--) {
    MicroSecondDelay (1);
  }
  return (Timeout == 0) ? EFI_TIMEOUT : EFI_SUCCESS;
}

STATIC
EFI_STATUS
UfsSendUicCommand (
  IN  UINT32  Cmd,
  IN  UINT32  Arg1,
  IN  UINT32  Arg2,
  IN  UINT32  Arg3
  )
{
  UINTN  Timeout;

  if (EFI_ERROR (UfsWaitHcReady ())) {
    return EFI_TIMEOUT;
  }

  MmioWrite32 (UFS_IS,      UFS_IS_UCCS);   // clear completion flag
  MmioWrite32 (UFS_UCMDARG1, Arg1);
  MmioWrite32 (UFS_UCMDARG2, Arg2);
  MmioWrite32 (UFS_UCMDARG3, Arg3);
  MmioWrite32 (UFS_UICCMD,   Cmd);

  Timeout = 500000;
  while (!(MmioRead32 (UFS_IS) & UFS_IS_UCCS) && Timeout--) {
    MicroSecondDelay (1);
  }
  if (Timeout == 0) {
    DEBUG ((DEBUG_ERROR, "UFS: UIC command 0x%02X timed out\n", Cmd));
    return EFI_TIMEOUT;
  }
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Host controller initialisation
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
UfsHostControllerInit (
  VOID
  )
{
  UINTN   Timeout;
  UINT32  Hcs;

  DEBUG ((DEBUG_INFO, "UFS: HC ver=0x%08X cap=0x%08X\n",
          MmioRead32 (UFS_VER), MmioRead32 (UFS_CAPABILITIES)));

  // Qualcomm: configure 1 μs clock period (19.2 MHz CXO → 19+1 = 20 counts ≈ 1 μs)
  MmioWrite32 (QCOM_UFS_SYS1CLK_1US, 0x13);

  //
  // Enable host controller — JEDEC UFSHCI 1.1 / Qualcomm sequence:
  //   Step 1: Write 0 to HCE to reset the HC (ensure clean state)
  //   Step 2: Poll until HCE reads 0 (HC is in reset)
  //   Step 3: Write 1 to HCE to start HC initialisation
  //   Step 4: Poll until HCE reads 1 (HC initialised and active)
  //
  // The bit does NOT self-clear in UFSHCI 1.1 — it stays set when HC is ready.
  // (UFSHCI 2.x changed this behaviour; UFSHCI 1.1 is different.)
  //
  MmioWrite32 (UFS_HCE, 0);
  Timeout = 50000;
  while ((MmioRead32 (UFS_HCE) & UFS_HCE_HCE) && Timeout--) {
    MicroSecondDelay (1);
  }
  if (MmioRead32 (UFS_HCE) & UFS_HCE_HCE) {
    DEBUG ((DEBUG_ERROR, "UFS: HC disable (reset) timed out\n"));
    return EFI_TIMEOUT;
  }

  MmioWrite32 (UFS_HCE, UFS_HCE_HCE);
  Timeout = 50000;   // 50 ms — spec requires HC ready within 1 ms
  while (!(MmioRead32 (UFS_HCE) & UFS_HCE_HCE) && Timeout--) {
    MicroSecondDelay (1);
  }
  if (!(MmioRead32 (UFS_HCE) & UFS_HCE_HCE)) {
    DEBUG ((DEBUG_ERROR, "UFS: HC enable timed out (HCE=0x%08X)\n",
            MmioRead32 (UFS_HCE)));
    return EFI_TIMEOUT;
  }

  // Polling mode: disable all interrupts
  MmioWrite32 (UFS_IE, 0);

  // DME_LINK_STARTUP — brings up the UniPro link between HC and device
  if (EFI_ERROR (UfsSendUicCommand (UFS_UIC_CMD_DME_LINK_STARTUP, 0, 0, 0))) {
    DEBUG ((DEBUG_ERROR, "UFS: DME_LINK_STARTUP failed\n"));
    return EFI_DEVICE_ERROR;
  }

  // Wait for device present (DP bit in HCS)
  Timeout = 2000000;  // 2 s
  do {
    Hcs = MmioRead32 (UFS_HCS);
    if (Hcs & UFS_HCS_DP) {
      break;
    }
    MicroSecondDelay (1);
  } while (Timeout--);

  if (!(MmioRead32 (UFS_HCS) & UFS_HCS_DP)) {
    DEBUG ((DEBUG_ERROR, "UFS: No device present (HCS=0x%08X)\n", MmioRead32 (UFS_HCS)));
    return EFI_NOT_FOUND;
  }

  // Allocate 4 KB DMA scratch buffer (must be naturally 4 KB aligned)
  mUfsDmaBuf = AllocateAlignedPages (1, SIZE_4KB);
  if (mUfsDmaBuf == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  ZeroMem (mUfsDmaBuf, UFS_DMA_BUF_SIZE);

  // Register the 1-entry UTRD list with the HC
  // UTRD list base = mUfsDmaBuf + UFS_UTRD_OFFSET (offset 0, 4 KB aligned → 1 KB aligned OK)
  {
    UINT64 UtrdPhys = (UINT64)(UINTN)(mUfsDmaBuf + UFS_UTRD_OFFSET);
    MmioWrite32 (UFS_UTRLBA,  (UINT32)(UtrdPhys & 0xFFFFFFFF));
    MmioWrite32 (UFS_UTRLBAU, (UINT32)(UtrdPhys >> 32));
  }
  MmioWrite32 (UFS_UTMRLBA,  0);
  MmioWrite32 (UFS_UTMRLBAU, 0);

  // Run the request lists
  MmioWrite32 (UFS_UTRLRSR,  1);
  MmioWrite32 (UFS_UTMRLRSR, 1);

  DEBUG ((DEBUG_INFO, "UFS: Link up, device present (HCS=0x%08X)\n", MmioRead32 (UFS_HCS)));
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// SCSI command execution via UPIU/UTRD
//
// Submits one SCSI CDB using the pre-allocated DMA buffer (slot 0).
// This is a synchronous polling implementation — correct for UEFI.
//
// The DMA scratch buffer layout used here:
//   [0..31]   UTRD[0]
//   [64..95]  Command UPIU     (CD base = &DmaBuf[64])
//   [96..127] Response UPIU
//   [128+]    PRDT entries
// ---------------------------------------------------------------------------
STATIC
EFI_STATUS
UfsExecScsiCommand (
  IN  UINT8   Lun,
  IN  UINT8   TaskTag,
  IN  UINT8  *Cdb,       // 16-byte CDB (zero-padded if shorter)
  IN  UINT8   CdbLen,
  IN  UINT32  DataLen,   // expected transfer length in bytes
  IN  VOID   *DataBuf,   // caller-supplied data buffer
  IN  BOOLEAN IsRead     // TRUE = device→host (READ), FALSE = host→device (WRITE)
  )
{
  UINT8   *Utrd;
  UINT8   *CmdUpiu;
  UINT8   *RspUpiu;
  UINT8   *Prdt;
  UINT64   CdPhys;
  UINT64   DataPhys;
  UINT32   NumPrdtEntries;
  UINT32   Remaining;
  UINT32   EntryIdx;
  UINT32   ChunkBytes;
  UINTN    Timeout;
  UINT32   Dw0;
  UINT8    OCS;

  if (mUfsDmaBuf == NULL) {
    return EFI_NOT_READY;
  }

  Utrd    = mUfsDmaBuf + UFS_UTRD_OFFSET;
  CmdUpiu = mUfsDmaBuf + UFS_CMD_UPIU_OFFSET;
  RspUpiu = mUfsDmaBuf + UFS_RSP_UPIU_OFFSET;
  Prdt    = mUfsDmaBuf + UFS_PRDT_OFFSET;

  // --- Build PRDT ---
  // One PRDT entry per 256 KB chunk (PRDT_MAX_BYTES_PER_ENTRY limit).
  // DataBuf must be physically contiguous (true for AllocatePages buffers).
  DataPhys      = (UINT64)(UINTN)DataBuf;
  Remaining     = DataLen;
  NumPrdtEntries = 0;
  EntryIdx      = 0;

  while (Remaining > 0 && EntryIdx < UFS_MAX_PRDT_ENTRIES) {
    UINT8  *PrdtEntry = Prdt + EntryIdx * 16;
    ChunkBytes = (Remaining > PRDT_MAX_BYTES_PER_ENTRY)
                    ? PRDT_MAX_BYTES_PER_ENTRY
                    : Remaining;

    UINT64 EntryAddr = DataPhys + (UINT64)EntryIdx * PRDT_MAX_BYTES_PER_ENTRY;
    // DW0: lower 32 bits of data address (bit[1:0] must be 0 — 4-byte aligned)
    ((UINT32 *)PrdtEntry)[0] = (UINT32)(EntryAddr & 0xFFFFFFFC);
    // DW1: upper 32 bits
    ((UINT32 *)PrdtEntry)[1] = (UINT32)(EntryAddr >> 32);
    // DW2: reserved
    ((UINT32 *)PrdtEntry)[2] = 0;
    // DW3: Data Byte Count - 1
    ((UINT32 *)PrdtEntry)[3] = ChunkBytes - 1;

    Remaining -= ChunkBytes;
    NumPrdtEntries++;
    EntryIdx++;
  }
  if (Remaining > 0) {
    DEBUG ((DEBUG_ERROR, "UFS: Transfer too large for PRDT (%u bytes)\n", DataLen));
    return EFI_INVALID_PARAMETER;
  }

  // --- Build Command UPIU ---
  ZeroMem (CmdUpiu, 32);
  CmdUpiu[0] = UPIU_TRANS_CODE_CMD;
  CmdUpiu[1] = IsRead ? UPIU_FLAG_READ : UPIU_FLAG_WRITE;
  CmdUpiu[2] = Lun;
  CmdUpiu[3] = TaskTag;
  CmdUpiu[4] = 0;          // Initiator ID
  CmdUpiu[5] = 0;          // Command Set Type = SCSI
  CmdUpiu[6] = 0;          // Reserved
  CmdUpiu[7] = 0;          // Status
  CmdUpiu[8] = 0;          // EHS Length
  CmdUpiu[9] = 0;          // Device Information
  // Bytes 10-11: Data Segment Length = 0 for Command UPIU
  CmdUpiu[10] = 0;
  CmdUpiu[11] = 0;
  // Bytes 12-15: Expected Data Transfer Length (big-endian)
  WriteBe32 (&CmdUpiu[12], DataLen);
  // Bytes 16-31: CDB
  CopyMem (&CmdUpiu[16], Cdb, (CdbLen <= 16) ? CdbLen : 16);

  // --- Build UTRD ---
  ZeroMem (Utrd, 32);
  CdPhys = (UINT64)(UINTN)CmdUpiu;   // CD base = start of Command UPIU

  Dw0 = UTRD_CT_SCSI;
  Dw0 |= IsRead ? UTRD_DD_READ : UTRD_DD_WRITE;
  // I bit (interrupt) = 0 — polling mode

  ((UINT32 *)Utrd)[0] = Dw0;
  ((UINT32 *)Utrd)[1] = 0;
  ((UINT32 *)Utrd)[2] = UTRD_OCS_INVALID;   // OCS: HC will overwrite this
  ((UINT32 *)Utrd)[3] = 0;
  ((UINT32 *)Utrd)[4] = (UINT32)(CdPhys & 0xFFFFFFFF);  // CD base low
  ((UINT32 *)Utrd)[5] = (UINT32)(CdPhys >> 32);          // CD base high
  // DW6: Response UPIU offset (DWORDs from CD base) | length (DWORDs)
  ((UINT32 *)Utrd)[6] = ((UINT32)UFS_RSP_UPIU_DWORD_OFFSET << 16) |
                         ((UINT32)(32 / 4));   // 32 bytes = 8 DWORDs
  // DW7: PRDT offset (DWORDs from CD base) | PRDT entry count
  ((UINT32 *)Utrd)[7] = ((UINT32)UFS_PRDT_DWORD_OFFSET << 16) |
                         (UINT32)NumPrdtEntries;

  // Flush: ensure HC sees our writes before we ring the doorbell.
  // WriteBackDataCacheRange flushes the cache lines to PoC.
  WriteBackDataCacheRange (mUfsDmaBuf, UFS_DMA_BUF_SIZE);
  if (DataLen > 0 && !IsRead) {
    // Write: flush caller's data buffer to PoC so device can read it
    WriteBackDataCacheRange (DataBuf, DataLen);
  }

  // --- Ring doorbell for slot 0 ---
  MmioWrite32 (UFS_IS, UFS_IS_UTRCS | UFS_IS_UE);   // clear completion/error status
  MmioWrite32 (UFS_UTRLDBR, BIT0);                   // submit slot 0

  // --- Poll for completion (UTRLDBR bit 0 clears when HC is done) ---
  Timeout = 5000000;   // 5 s for worst-case UFS command
  while ((MmioRead32 (UFS_UTRLDBR) & BIT0) && Timeout--) {
    MicroSecondDelay (1);
  }

  // Invalidate our DMA buffer so we see HC's writes (OCS, Response UPIU)
  InvalidateDataCacheRange (mUfsDmaBuf, UFS_DMA_BUF_SIZE);

  if (MmioRead32 (UFS_UTRLDBR) & BIT0) {
    // Doorbell still set → HC timed out; clear it
    MmioWrite32 (UFS_UTRLCLR, 0);
    DEBUG ((DEBUG_ERROR, "UFS: SCSI command timed out (IS=0x%08X)\n",
            MmioRead32 (UFS_IS)));
    return EFI_TIMEOUT;
  }

  // Check for HC-level error
  if (MmioRead32 (UFS_IS) & UFS_IS_UE) {
    DEBUG ((DEBUG_ERROR, "UFS: HC error (IS=0x%08X UECPA=0x%08X)\n",
            MmioRead32 (UFS_IS), MmioRead32 (UFS_UECPA)));
    return EFI_DEVICE_ERROR;
  }

  // Check OCS (Overall Command Status) — HC writes this in UTRD DW2
  OCS = (UINT8)(((UINT32 *)Utrd)[2] & 0xFF);
  if (OCS != 0x00) {
    DEBUG ((DEBUG_ERROR, "UFS: Bad OCS=0x%02X for SCSI command 0x%02X\n",
            OCS, Cdb[0]));
    return EFI_DEVICE_ERROR;
  }

  // Check SCSI status in Response UPIU byte 7 (0x00 = GOOD)
  if (RspUpiu[7] != 0x00) {
    DEBUG ((DEBUG_ERROR, "UFS: SCSI SENSE status=0x%02X\n", RspUpiu[7]));
    return EFI_DEVICE_ERROR;
  }

  // For reads: invalidate caller's data buffer so CPU sees device-written data
  if (DataLen > 0 && IsRead) {
    InvalidateDataCacheRange (DataBuf, DataLen);
  }

  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// BlockIo protocol implementation
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
EFIAPI
UfsReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                 ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UfsReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 Lba,
  IN  UINTN                   BufferSize,
  OUT VOID                   *Buffer
  )
{
  UFS_LUN_DEVICE  *Dev = BASE_CR (This, UFS_LUN_DEVICE, BlockIo);
  EFI_STATUS       Status;
  UINT8            Cdb[16];
  UINTN            BlocksRemaining;
  UINTN            CurrentLba;
  UINTN            ByteOffset;
  UINTN            BlocksThisPass;
  UINTN            BytesThisPass;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if (MediaId != Dev->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  if ((BufferSize % Dev->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  BlocksRemaining = BufferSize / Dev->Media.BlockSize;
  CurrentLba      = (UINTN)Lba;
  ByteOffset      = 0;

  // Submit reads in chunks of up to 0xFFFF blocks (READ(10) limit)
  // and up to (UFS_MAX_PRDT_ENTRIES × 256 KB) bytes per command.
  // In practice UEFI reads are tiny so one pass covers it.
  while (BlocksRemaining > 0) {
    BlocksThisPass = BlocksRemaining;
    if (BlocksThisPass > 0xFFFF) {
      BlocksThisPass = 0xFFFF;
    }
    BytesThisPass  = BlocksThisPass * Dev->Media.BlockSize;

    // SCSI READ(10): CDB[0]=0x28, LBA big-endian at [2..5], xfer-len at [7..8]
    ZeroMem (Cdb, 16);
    Cdb[0]  = SCSI_READ10;
    Cdb[1]  = (Dev->Lun & 0x07) << 5;   // LUN in bits [7:5] of byte 1 (SCSI legacy)
    Cdb[2]  = (UINT8)(CurrentLba >> 24);
    Cdb[3]  = (UINT8)(CurrentLba >> 16);
    Cdb[4]  = (UINT8)(CurrentLba >>  8);
    Cdb[5]  = (UINT8)(CurrentLba >>  0);
    Cdb[7]  = (UINT8)(BlocksThisPass >> 8);
    Cdb[8]  = (UINT8)(BlocksThisPass >> 0);

    Status = UfsExecScsiCommand (
               Dev->Lun, 0x01,
               Cdb, 10,
               (UINT32)BytesThisPass,
               (UINT8 *)Buffer + ByteOffset,
               TRUE
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "UFS: ReadBlocks LUN%d LBA=0x%LX failed: %r\n",
              Dev->Lun, (UINT64)CurrentLba, Status));
      return Status;
    }

    CurrentLba      += BlocksThisPass;
    BlocksRemaining -= BlocksThisPass;
    ByteOffset      += BytesThisPass;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UfsWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                  MediaId,
  IN EFI_LBA                 Lba,
  IN UINTN                   BufferSize,
  IN VOID                   *Buffer
  )
{
  UFS_LUN_DEVICE  *Dev = BASE_CR (This, UFS_LUN_DEVICE, BlockIo);
  EFI_STATUS       Status;
  UINT8            Cdb[16];
  UINTN            BlocksRemaining;
  UINTN            CurrentLba;
  UINTN            ByteOffset;
  UINTN            BlocksThisPass;
  UINTN            BytesThisPass;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if (MediaId != Dev->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  if (Dev->Media.ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }
  if ((BufferSize % Dev->Media.BlockSize) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  BlocksRemaining = BufferSize / Dev->Media.BlockSize;
  CurrentLba      = (UINTN)Lba;
  ByteOffset      = 0;

  while (BlocksRemaining > 0) {
    BlocksThisPass = BlocksRemaining;
    if (BlocksThisPass > 0xFFFF) {
      BlocksThisPass = 0xFFFF;
    }
    BytesThisPass  = BlocksThisPass * Dev->Media.BlockSize;

    // SCSI WRITE(10)
    ZeroMem (Cdb, 16);
    Cdb[0]  = SCSI_WRITE10;
    Cdb[1]  = (Dev->Lun & 0x07) << 5;
    Cdb[2]  = (UINT8)(CurrentLba >> 24);
    Cdb[3]  = (UINT8)(CurrentLba >> 16);
    Cdb[4]  = (UINT8)(CurrentLba >>  8);
    Cdb[5]  = (UINT8)(CurrentLba >>  0);
    Cdb[7]  = (UINT8)(BlocksThisPass >> 8);
    Cdb[8]  = (UINT8)(BlocksThisPass >> 0);

    Status = UfsExecScsiCommand (
               Dev->Lun, 0x01,
               Cdb, 10,
               (UINT32)BytesThisPass,
               (UINT8 *)Buffer + ByteOffset,
               FALSE
               );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "UFS: WriteBlocks LUN%d LBA=0x%LX failed: %r\n",
              Dev->Lun, (UINT64)CurrentLba, Status));
      return Status;
    }

    CurrentLba      += BlocksThisPass;
    BlocksRemaining -= BlocksThisPass;
    ByteOffset      += BytesThisPass;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
UfsFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver entry point
// ---------------------------------------------------------------------------

EFI_STATUS
EFIAPI
UfsDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS      Status;
  UFS_LUN_DEVICE  *Lun;
  EFI_HANDLE      Handle;

  DEBUG ((DEBUG_INFO, "UfsDxe: MSM8996 UFSHCI @ 0x%08X\n", MSM8996_UFS_BASE));

  Status = UfsHostControllerInit ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "UfsDxe: HC init failed (%r)\n", Status));
    return Status;
  }

  // Install BlockIo for LUN 0 (main NAND storage, contains GPT + all Android partitions)
  Lun = AllocateZeroPool (sizeof (UFS_LUN_DEVICE));
  if (Lun == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Lun->Lun = 0;
  Lun->Media.MediaId          = 1;
  Lun->Media.RemovableMedia   = FALSE;
  Lun->Media.MediaPresent     = TRUE;
  Lun->Media.LogicalPartition = FALSE;
  Lun->Media.ReadOnly         = FALSE;
  Lun->Media.WriteCaching     = FALSE;
  Lun->Media.BlockSize        = UFS_BLOCK_SIZE;
  Lun->Media.LastBlock        = UFS_INTERNAL_STORAGE_BLOCKS - 1;

  Lun->BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION3;
  Lun->BlockIo.Media       = &Lun->Media;
  Lun->BlockIo.Reset       = UfsReset;
  Lun->BlockIo.ReadBlocks  = UfsReadBlocks;
  Lun->BlockIo.WriteBlocks = UfsWriteBlocks;
  Lun->BlockIo.FlushBlocks = UfsFlushBlocks;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiBlockIoProtocolGuid, &Lun->BlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Lun);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "UfsDxe: LUN0 BlockIo installed "
                      "(0x%LX × %u-byte blocks = 128 GB)\n",
          Lun->Media.LastBlock + 1, Lun->Media.BlockSize));
  return EFI_SUCCESS;
}
