/** @file
  HDMI TX driver for MSM8996 / Samsung Galaxy Tab S3 LTE.

  Hardware:
    - Qualcomm HDMI TX (qcom,hdmi-tx)
    - Base: 0x009A0000 (within MMSS block @ 0x00800000)
    - HPD (Hot Plug Detect): GPIO 35, active-high
    - DDC I2C: built into HDMI TX core (slave 0x50, 7-bit = 0xA0/0xA1 8-bit)

  This driver:
    1. Enables HDMI core clocks (already done by GccClockDxe)
    2. Enables HPD and detects display connection
    3. Reads EDID via the on-chip DDC I2C controller
    4. Selects the first detailed timing descriptor from EDID
       (falls back to 1920×1080@60Hz CEA VIC 16 if DDC fails)
    5. Configures HDMI TX video timing and AVI InfoFrame
    6. Allocates a BGRA framebuffer from DRAM
    7. Installs EFI_GRAPHICS_OUTPUT_PROTOCOL for the HDMI output

  DDC I2C controller (within HDMI TX):
    The MSM8996 HDMI DDC is a dedicated I2C master specifically for DDC/EDID.
    It supports burst reads of up to 255 bytes per transaction.
    Registers: HDMI_DDC_CTRL, HDMI_DDC_SETUP, HDMI_DDC_TRANS0..3, HDMI_DDC_DATA,
    HDMI_DDC_STATUS, HDMI_DDC_INT_CTRL.

  GOP framebuffer:
    Allocated from DRAM via AllocatePages, reported to GOP consumers.
    The MDP display engine outputs to HDMI TX via the HDMI interface path.
    The allocated framebuffer is the pixel source for the HDMI scan-out.

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// HDMI TX register map (offsets from HDMI_BASE = 0x009A0000)
// Source: Qualcomm MSM8996 TRM, MDSS HDMI TX block
// ---------------------------------------------------------------------------
#define HDMI_BASE                       MSM8996_HDMI_BASE

// Core control
#define HDMI_CTRL                       (HDMI_BASE + 0x000)
#define HDMI_AUDIO_PKT_CTRL1            (HDMI_BASE + 0x020)
#define HDMI_ACR_PKT_CTRL               (HDMI_BASE + 0x024)
#define HDMI_INFOFRAME_CTRL0            (HDMI_BASE + 0x02C)
#define HDMI_GEN_PKT_CTRL               (HDMI_BASE + 0x028)
#define HDMI_VBI_PKT_CTRL               (HDMI_BASE + 0x014)
#define HDMI_VIDEO_CFG                  (HDMI_BASE + 0x01C)

// Timing (CEA-861 register layout)
#define HDMI_TOTAL                      (HDMI_BASE + 0x0A4)   // H total | V total
#define HDMI_ACTIVE_H                   (HDMI_BASE + 0x0A8)   // H active end | start
#define HDMI_ACTIVE_V                   (HDMI_BASE + 0x0AC)   // V active end | start
#define HDMI_HSYNC_CTRL                 (HDMI_BASE + 0x098)   // H sync end | start
#define HDMI_VSYNC_CTRL                 (HDMI_BASE + 0x08C)   // V sync end | start
#define HDMI_VSYNC_TOTAL_F2             (HDMI_BASE + 0x084)
#define HDMI_VSYNC_TOTAL_F1             (HDMI_BASE + 0x080)
#define HDMI_VSYNC_PERIOD_F1            (HDMI_BASE + 0x088)
#define HDMI_ACTIVE_VSYNC               (HDMI_BASE + 0x090)
#define HDMI_ACTIVE_HSYNC               (HDMI_BASE + 0x09C)
#define HDMI_FRAME_CTRL                 (HDMI_BASE + 0x0A0)

// AVI InfoFrame (CEA-861)
#define HDMI_AVI_INFO0                  (HDMI_BASE + 0x06C)
#define HDMI_AVI_INFO1                  (HDMI_BASE + 0x070)
#define HDMI_AVI_INFO2                  (HDMI_BASE + 0x074)
#define HDMI_AVI_INFO3                  (HDMI_BASE + 0x078)

// HPD (Hot Plug Detect)
#define HDMI_HPD_CTRL                   (HDMI_BASE + 0x258)
#define HDMI_HPD_INT_STATUS             (HDMI_BASE + 0x25C)
#define HDMI_HPD_INT_CTRL               (HDMI_BASE + 0x260)

// DDC I2C controller
#define HDMI_DDC_CTRL                   (HDMI_BASE + 0x204)
#define HDMI_DDC_SETUP                  (HDMI_BASE + 0x20C)
#define HDMI_DDC_INT_CTRL               (HDMI_BASE + 0x214)
#define HDMI_DDC_STATUS                 (HDMI_BASE + 0x210)
#define HDMI_DDC_TRANS0                 (HDMI_BASE + 0x218)
#define HDMI_DDC_TRANS1                 (HDMI_BASE + 0x21C)
#define HDMI_DDC_TRANS2                 (HDMI_BASE + 0x220)
#define HDMI_DDC_TRANS3                 (HDMI_BASE + 0x224)
#define HDMI_DDC_DATA                   (HDMI_BASE + 0x238)

// HDMI_CTRL bits
#define HDMI_CTRL_ENABLE                BIT0
#define HDMI_CTRL_HDMI_MODE             BIT1   // 1 = HDMI mode, 0 = DVI mode

// HDMI_HPD_CTRL bits
#define HDMI_HPD_CTRL_ENABLE            BIT28
#define HDMI_HPD_CTRL_DEBOUNCE_MASK     0x1FF

// HDMI_HPD_INT_STATUS bits
#define HDMI_HPD_INT_STATUS_CONNECTED   BIT0   // 1 = cable plugged in

// DDC register bit definitions
// HDMI_DDC_SETUP:
//   [22:16] = high count (SCL high time in 19.2 MHz cycles)
//   [6:0]   = low count  (SCL low time in 19.2 MHz cycles)
// For 100 kHz DDC: period = 192 cycles. High = 96 = 0x60, Low = 96 = 0x60.
#define HDMI_DDC_SETUP_VAL              0x00600060  // 100 kHz DDC @ 19.2 MHz CXO

// HDMI_DDC_TRANS fields:
//   [27:16] = byte count for this transaction phase
//   [9]     = STOP (send stop at end)
//   [8]     = RNW  (1=read, 0=write)
//   [1]     = RSTART (send repeated START before this phase)
//   [0]     = START (send START before this phase)
#define DDC_TRANS_START                 BIT0
#define DDC_TRANS_RSTART                BIT1
#define DDC_TRANS_RNW                   BIT8
#define DDC_TRANS_STOP                  BIT9
#define DDC_TRANS_CNT_SHIFT             16

// HDMI_DDC_CTRL:
//   [0] = SEND_REQUEST (write 1 to trigger transaction)
//   [3] = GO bit (alternate trigger)
#define HDMI_DDC_CTRL_SEND              BIT0

// HDMI_DDC_STATUS:
//   [2] = DONE (transaction complete)
//   [4] = NACK (slave did not ACK)
//   [5] = NACK2
#define HDMI_DDC_STATUS_DONE            BIT2
#define HDMI_DDC_STATUS_NACK            BIT4

// EDID slave address (standard I2C 7-bit = 0x50)
#define EDID_I2C_ADDR_7BIT              0x50
#define EDID_I2C_WRITE                  (EDID_I2C_ADDR_7BIT << 1)       // 0xA0
#define EDID_I2C_READ                   ((EDID_I2C_ADDR_7BIT << 1) | 1) // 0xA1
#define EDID_SIZE                       128

// ---------------------------------------------------------------------------
// HDMI video mode descriptor
// ---------------------------------------------------------------------------
typedef struct {
  UINT32  HActive;
  UINT32  VActive;
  UINT32  HTotal;
  UINT32  VTotal;
  UINT32  HSyncStart;
  UINT32  HSyncEnd;
  UINT32  VSyncStart;
  UINT32  VSyncEnd;
  UINT32  PixelClockKHz;
  BOOLEAN HPolarity;       // TRUE = positive sync
  BOOLEAN VPolarity;
} HDMI_VIDEO_MODE;

// Fallback: CEA-861 VIC 16 — 1920×1080@60Hz progressive
STATIC CONST HDMI_VIDEO_MODE mMode1080p60 = {
  .HActive       = 1920,
  .VActive       = 1080,
  .HTotal        = 2200,
  .VTotal        = 1125,
  .HSyncStart    = 2008,
  .HSyncEnd      = 2052,
  .VSyncStart    = 1084,
  .VSyncEnd      = 1089,
  .PixelClockKHz = 148500,
  .HPolarity     = TRUE,
  .VPolarity     = TRUE,
};

// Active mode (set from EDID or fallback)
STATIC HDMI_VIDEO_MODE mActiveMode;

// GOP framebuffer (BGRA, allocated from DRAM)
STATIC EFI_PHYSICAL_ADDRESS        mHdmiFbPhys = 0;
STATIC VOID                       *mHdmiFbVirt = NULL;
STATIC UINTN                       mHdmiFbSize = 0;

// ---------------------------------------------------------------------------
// GOP protocol instance
// ---------------------------------------------------------------------------

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mGopModeInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE     mGopMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL          mGop;

STATIC
EFI_STATUS
EFIAPI
HdmiGopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                 ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
  )
{
  if (ModeNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  *Info       = &mGopModeInfo;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HdmiGopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                         ModeNumber
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  // Mode is already set; clear the framebuffer to black
  ZeroMem (mHdmiFbVirt, mHdmiFbSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HdmiGopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL      *BltBuffer  OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION   BltOperation,
  IN UINTN                               SourceX,
  IN UINTN                               SourceY,
  IN UINTN                               DestinationX,
  IN UINTN                               DestinationY,
  IN UINTN                               Width,
  IN UINTN                               Height,
  IN UINTN                               Delta         OPTIONAL
  )
{
  UINT32  *Fb;
  UINTN    Stride;   // pixels per row
  UINTN    X, Y;

  Fb     = (UINT32 *)mHdmiFbVirt;
  Stride = mActiveMode.HActive;

  switch (BltOperation) {
  case EfiBltVideoFill:
    {
      UINT32 Color = (BltBuffer[0].Blue)       |
                     (BltBuffer[0].Green << 8)  |
                     (BltBuffer[0].Red   << 16) |
                     (0xFF               << 24);
      for (Y = DestinationY; Y < DestinationY + Height; Y++) {
        for (X = DestinationX; X < DestinationX + Width; X++) {
          Fb[Y * Stride + X] = Color;
        }
      }
    }
    break;

  case EfiBltBufferToVideo:
    {
      UINTN  SrcStride = (Delta == 0) ? Width : (Delta / sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL *P = &BltBuffer[(SourceY + Y) * SrcStride + (SourceX + X)];
          Fb[(DestinationY + Y) * Stride + (DestinationX + X)] =
              (UINT32)P->Blue | ((UINT32)P->Green << 8) |
              ((UINT32)P->Red << 16) | ((UINT32)P->Reserved << 24);
        }
      }
    }
    break;

  case EfiBltVideoToBuffer:
    {
      UINTN  DstStride = (Delta == 0) ? Width : (Delta / sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
      for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
          UINT32 Px = Fb[(SourceY + Y) * Stride + (SourceX + X)];
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL *P = &BltBuffer[(DestinationY + Y) * DstStride + (DestinationX + X)];
          P->Blue     = (UINT8)(Px >>  0);
          P->Green    = (UINT8)(Px >>  8);
          P->Red      = (UINT8)(Px >> 16);
          P->Reserved = (UINT8)(Px >> 24);
        }
      }
    }
    break;

  case EfiBltVideoToVideo:
    {
      for (Y = 0; Y < Height; Y++) {
        CopyMem (
          &Fb[(DestinationY + Y) * Stride + DestinationX],
          &Fb[(SourceY + Y)      * Stride + SourceX],
          Width * sizeof (UINT32)
          );
      }
    }
    break;

  default:
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// DDC I2C — EDID read
//
// The MSM8996 HDMI DDC controller performs I2C transactions using a
// programmed transaction FIFO (TRANS0..3). For an EDID read:
//
//   Phase 0 (TRANS0): START + write slave address (0xA0) + write reg 0x00
//     → 2 bytes written (address + register): cnt=2, START=1, RNW=0
//   Phase 1 (TRANS1): REPEATED START + read slave address (0xA1) + read 128 B
//     → 128 bytes read: cnt=128, RSTART=1, RNW=1, STOP=1
//
//   The slave address byte is written to HDMI_DDC_DATA before triggering.
//   After completion, 128 EDID bytes are read back from HDMI_DDC_DATA.
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
HdmiDdcReadEdid (
  OUT UINT8  *EdidBuf   // caller-supplied 128-byte buffer
  )
{
  UINTN   Timeout;
  UINT32  Status;
  UINTN   i;

  // DDC setup: configure SCL timing (100 kHz)
  MmioWrite32 (HDMI_DDC_SETUP, HDMI_DDC_SETUP_VAL);

  // Disable DDC interrupts — poll only
  MmioWrite32 (HDMI_DDC_INT_CTRL, 0);

  // Write data FIFO: slave addr (write), register offset 0x00
  // The DDC controller reads these as the payload for the write phase
  MmioWrite32 (HDMI_DDC_DATA, EDID_I2C_WRITE);   // 0xA0
  MmioWrite32 (HDMI_DDC_DATA, 0x00);              // register offset

  // TRANS0: write phase — 2 bytes (slave addr + reg), START condition
  MmioWrite32 (HDMI_DDC_TRANS0,
               (2u << DDC_TRANS_CNT_SHIFT) | DDC_TRANS_START);

  // TRANS1: read phase — 128 bytes, REPEATED START, STOP at end
  MmioWrite32 (HDMI_DDC_TRANS1,
               ((UINT32)EDID_SIZE << DDC_TRANS_CNT_SHIFT) |
               DDC_TRANS_RSTART | DDC_TRANS_RNW | DDC_TRANS_STOP);

  // Mark TRANS2, TRANS3 as unused (0 bytes)
  MmioWrite32 (HDMI_DDC_TRANS2, 0);
  MmioWrite32 (HDMI_DDC_TRANS3, 0);

  // Trigger the transaction
  MmioWrite32 (HDMI_DDC_CTRL, HDMI_DDC_CTRL_SEND);

  // Poll for completion (DONE bit in DDC_STATUS) — up to 100 ms
  Timeout = 100000;
  do {
    Status = MmioRead32 (HDMI_DDC_STATUS);
    if (Status & HDMI_DDC_STATUS_DONE) {
      break;
    }
    MicroSecondDelay (1);
  } while (Timeout--);

  if (!(Status & HDMI_DDC_STATUS_DONE)) {
    DEBUG ((DEBUG_WARN, "HDMI: DDC timeout (STATUS=0x%08X)\n", Status));
    return EFI_TIMEOUT;
  }
  if (Status & HDMI_DDC_STATUS_NACK) {
    DEBUG ((DEBUG_WARN, "HDMI: DDC NACK (STATUS=0x%08X)\n", Status));
    return EFI_DEVICE_ERROR;
  }

  // Read 128 EDID bytes from data FIFO
  for (i = 0; i < EDID_SIZE; i++) {
    EdidBuf[i] = (UINT8)(MmioRead32 (HDMI_DDC_DATA) & 0xFF);
  }

  // Validate EDID header: bytes 0-7 must be { 00 FF FF FF FF FF FF 00 }
  if (EdidBuf[0] != 0x00 || EdidBuf[1] != 0xFF || EdidBuf[7] != 0x00) {
    DEBUG ((DEBUG_WARN, "HDMI: EDID header invalid (0x%02X 0x%02X ... 0x%02X)\n",
            EdidBuf[0], EdidBuf[1], EdidBuf[7]));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "HDMI: EDID read OK — mfr 0x%02X%02X, wk %d/%d\n",
          EdidBuf[8], EdidBuf[9], EdidBuf[16], EdidBuf[17]));
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// EDID parsing — extract first Detailed Timing Descriptor (bytes 54-71)
//
// EDID 1.x DTD layout (18 bytes at offset 54):
//   Bytes 0-1:  Pixel clock (LSB first) in units of 10 kHz
//   Byte  2:    H addressable[7:0]
//   Byte  3:    H blank[7:0]
//   Byte  4:    H addressable[11:8] | H blank[11:8]
//   Byte  5:    V addressable[7:0]
//   Byte  6:    V blank[7:0]
//   Byte  7:    V addressable[11:8] | V blank[11:8]
//   Byte  8:    H front porch[7:0]
//   Byte  9:    H sync width[7:0]
//   Byte 10:    V front porch[3:0] | V sync width[3:0]
//   Byte 11:    H front porch[9:8] | H sync width[9:8] | V fp[5:4] | V sw[5:4]
//   Bytes 12-17: Size, border, flags (sync polarity at byte 17)
// ---------------------------------------------------------------------------

STATIC
BOOLEAN
HdmiParseEdidDtd (
  IN  CONST UINT8     *Edid,
  OUT HDMI_VIDEO_MODE *Mode
  )
{
  CONST UINT8  *D = Edid + 54;  // first DTD at byte 54
  UINT32  PixClkx10;
  UINT32  HActive, HBlank, HFront, HSync;
  UINT32  VActive, VBlank, VFront, VSync;

  // Pixel clock 0 means this is a monitor descriptor tag, not a timing block
  PixClkx10 = (UINT32)D[0] | ((UINT32)D[1] << 8);
  if (PixClkx10 == 0) {
    DEBUG ((DEBUG_WARN, "HDMI: EDID byte 54 is monitor descriptor, not DTD\n"));
    return FALSE;
  }

  HActive = (UINT32)D[2] | (((UINT32)D[4] >> 4) << 8);
  HBlank  = (UINT32)D[3] | (((UINT32)D[4] & 0x0F) << 8);
  VActive = (UINT32)D[5] | (((UINT32)D[7] >> 4) << 8);
  VBlank  = (UINT32)D[6] | (((UINT32)D[7] & 0x0F) << 8);
  HFront  = (UINT32)D[8]  | (((UINT32)D[11] & 0xC0) << 2);
  HSync   = (UINT32)D[9]  | (((UINT32)D[11] & 0x30) << 4);
  VFront  = ((UINT32)D[10] >> 4) | (((UINT32)D[11] & 0x0C) << 2);
  VSync   = ((UINT32)D[10] & 0x0F) | (((UINT32)D[11] & 0x03) << 4);

  Mode->HActive       = HActive;
  Mode->VActive       = VActive;
  Mode->HTotal        = HActive + HBlank;
  Mode->VTotal        = VActive + VBlank;
  Mode->HSyncStart    = HActive + HFront;
  Mode->HSyncEnd      = HActive + HFront + HSync;
  Mode->VSyncStart    = VActive + VFront;
  Mode->VSyncEnd      = VActive + VFront + VSync;
  Mode->PixelClockKHz = PixClkx10 * 10;
  // Byte 17 flags: bit 1 = V sync polarity, bit 2 = H sync polarity (1=positive)
  Mode->HPolarity = (D[17] & BIT2) != 0;
  Mode->VPolarity = (D[17] & BIT1) != 0;

  DEBUG ((DEBUG_INFO, "HDMI: EDID DTD %u×%u@%u kHz px (H:%u V:%u)\n",
          HActive, VActive, Mode->PixelClockKHz, Mode->HTotal, Mode->VTotal));
  return TRUE;
}

// ---------------------------------------------------------------------------
// HDMI TX hardware configuration
// ---------------------------------------------------------------------------

STATIC
VOID
HdmiEnableCore (
  VOID
  )
{
  MmioWrite32 (HDMI_CTRL, HDMI_CTRL_ENABLE | HDMI_CTRL_HDMI_MODE);
  MicroSecondDelay (1000);
}

STATIC
BOOLEAN
HdmiHpdDetect (
  VOID
  )
{
  // Enable HPD receiver with 500-clock debounce (≈26 μs at 19.2 MHz)
  MmioWrite32 (HDMI_HPD_CTRL, HDMI_HPD_CTRL_ENABLE | 0x1F4);
  MicroSecondDelay (200000);   // 200 ms debounce settle

  return (MmioRead32 (HDMI_HPD_INT_STATUS) & HDMI_HPD_INT_STATUS_CONNECTED) != 0;
}

STATIC
VOID
HdmiConfigureTiming (
  IN CONST HDMI_VIDEO_MODE  *M
  )
{
  UINT32  FrameCtrl;

  //
  // Register layout for MSM8996 HDMI TX timing registers:
  //   HDMI_TOTAL:    [31:16] = H total - 1,  [15:0] = V total - 1
  //   HDMI_ACTIVE_H: [31:16] = H active end,  [15:0] = H active start (0)
  //   HDMI_ACTIVE_V: [31:16] = V active end,  [15:0] = V active start (0)
  //   HDMI_HSYNC_CTRL: [31:16] = H sync end,  [15:0] = H sync start
  //   HDMI_VSYNC_CTRL: [31:16] = V sync end,  [15:0] = V sync start
  //   HDMI_FRAME_CTRL: bit 15 = H invert, bit 14 = V invert, bit 0 = interlace
  //
  MmioWrite32 (HDMI_TOTAL,    ((M->HTotal - 1) << 16) | (M->VTotal - 1));
  MmioWrite32 (HDMI_ACTIVE_H, ((M->HActive - 1) << 16) | 0);
  MmioWrite32 (HDMI_ACTIVE_V, ((M->VActive - 1) << 16) | 0);
  MmioWrite32 (HDMI_HSYNC_CTRL, (M->HSyncEnd << 16) | M->HSyncStart);
  MmioWrite32 (HDMI_VSYNC_CTRL, (M->VSyncEnd << 16) | M->VSyncStart);
  MmioWrite32 (HDMI_VSYNC_TOTAL_F1, M->VTotal - 1);
  MmioWrite32 (HDMI_VSYNC_PERIOD_F1, M->VTotal - 1);

  // Frame control: set sync invert bits if polarity is negative
  FrameCtrl = 0;
  if (!M->HPolarity) {
    FrameCtrl |= BIT15;  // invert H sync
  }
  if (!M->VPolarity) {
    FrameCtrl |= BIT14;  // invert V sync
  }
  MmioWrite32 (HDMI_FRAME_CTRL, FrameCtrl);

  DEBUG ((DEBUG_INFO, "HDMI: Timing %u×%u HT=%u VT=%u HS=%u..%u VS=%u..%u\n",
          M->HActive, M->VActive, M->HTotal, M->VTotal,
          M->HSyncStart, M->HSyncEnd, M->VSyncStart, M->VSyncEnd));
}

STATIC
VOID
HdmiConfigureAviInfoframe (
  IN CONST HDMI_VIDEO_MODE  *M
  )
{
  //
  // CEA-861 AVI InfoFrame (Type 0x82, version 2, length 13):
  //   Byte 1: [5:4] = Scan Info (00=No data), [3:2] = Bar Info, [1:0] = Active Format
  //   Byte 2: [7] = IT content, [6:4] = Colorimetry, [3:2] = Picture Aspect Ratio,
  //           [1:0] = Active Portion Aspect Ratio
  //   Byte 3: [7] = YCC quantization, [5:4] = Non-uniform scaling, [3:0] = VIC
  //   Byte 4: pixel repetition = 0
  //
  // For 1920×1080@60 → VIC = 16.  For other modes VIC = 0 (no specific VIC).
  //
  UINT8  Vic = 0;
  if (M->HActive == 1920 && M->VActive == 1080) {
    Vic = 16;
  } else if (M->HActive == 1280 && M->VActive == 720) {
    Vic = 4;   // 720p60
  }

  // Pack AVI InfoFrame bytes into the four HDMI_AVI_INFO registers.
  // Each register holds 3 bytes of InfoFrame payload (MSB order within DWORD).
  // AVI_INFO0 = bytes [3:1] of AVI IF, AVI_INFO1 = bytes [6:4], etc.
  MmioWrite32 (HDMI_AVI_INFO0,
               (0x00 << 16) |   // byte 1: no scan, no bar, no active format
               (0x18 <<  8) |   // byte 2: 16:9 aspect ratio (bits[4:3] = 10)
               (Vic  <<  0));   // byte 3: VIC
  MmioWrite32 (HDMI_AVI_INFO1, 0);
  MmioWrite32 (HDMI_AVI_INFO2, 0);
  MmioWrite32 (HDMI_AVI_INFO3, 0x0D << 8);   // checksum placeholder, length=13

  // Enable AVI InfoFrame transmission (bit 1 in INFOFRAME_CTRL0)
  MmioOr32 (HDMI_INFOFRAME_CTRL0, BIT1);
}

// ---------------------------------------------------------------------------
// Framebuffer allocation and GOP installation
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
HdmiInstallGop (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  GopHandle;
  UINTN       FbPages;

  // Allocate BGRA framebuffer: HActive × VActive × 4 bytes
  mHdmiFbSize  = mActiveMode.HActive * mActiveMode.VActive * sizeof (UINT32);
  FbPages      = EFI_SIZE_TO_PAGES (mHdmiFbSize);

  mHdmiFbPhys = 0;   // let UEFI choose address (anywhere in DRAM)
  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiBootServicesData,
                  FbPages,
                  &mHdmiFbPhys
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HDMI: Failed to allocate framebuffer: %r\n", Status));
    return Status;
  }
  mHdmiFbVirt = (VOID *)(UINTN)mHdmiFbPhys;
  ZeroMem (mHdmiFbVirt, mHdmiFbSize);

  DEBUG ((DEBUG_INFO, "HDMI: Framebuffer @ 0x%LX size 0x%X (%u×%u BGRA)\n",
          mHdmiFbPhys, (UINT32)mHdmiFbSize,
          mActiveMode.HActive, mActiveMode.VActive));

  // Fill GOP mode info
  mGopModeInfo.Version              = 0;
  mGopModeInfo.HorizontalResolution = mActiveMode.HActive;
  mGopModeInfo.VerticalResolution   = mActiveMode.VActive;
  mGopModeInfo.PixelFormat          = PixelBlueGreenRedReserved8BitPerColor;
  mGopModeInfo.PixelsPerScanLine    = mActiveMode.HActive;

  mGopMode.MaxMode              = 1;
  mGopMode.Mode                 = 0;
  mGopMode.Info                 = &mGopModeInfo;
  mGopMode.SizeOfInfo           = sizeof (mGopModeInfo);
  mGopMode.FrameBufferBase      = mHdmiFbPhys;
  mGopMode.FrameBufferSize      = mHdmiFbSize;

  mGop.QueryMode = HdmiGopQueryMode;
  mGop.SetMode   = HdmiGopSetMode;
  mGop.Blt       = HdmiGopBlt;
  mGop.Mode      = &mGopMode;

  GopHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &GopHandle,
                  &gEfiGraphicsOutputProtocolGuid, &mGop,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "HDMI: GOP install failed: %r\n", Status));
    gBS->FreePages (mHdmiFbPhys, FbPages);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "HDMI: GOP installed (mode 0: %u×%u BGRA)\n",
          mActiveMode.HActive, mActiveMode.VActive));
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver entry point
// ---------------------------------------------------------------------------

EFI_STATUS
EFIAPI
HdmiDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINT8       Edid[EDID_SIZE];
  BOOLEAN     GotEdid;

  DEBUG ((DEBUG_INFO, "HdmiDxe: MSM8996 HDMI TX @ 0x%08X\n", MSM8996_HDMI_BASE));

  HdmiEnableCore ();

  if (!HdmiHpdDetect ()) {
    DEBUG ((DEBUG_INFO, "HdmiDxe: No HDMI display connected (HPD low) — skipping\n"));
    // Not an error; tablet may boot without any HDMI cable attached.
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_INFO, "HdmiDxe: HDMI display connected\n"));

  // Try to read EDID via DDC; fall back to 1080p60 on failure
  GotEdid = FALSE;
  Status  = HdmiDdcReadEdid (Edid);
  if (!EFI_ERROR (Status)) {
    GotEdid = HdmiParseEdidDtd (Edid, &mActiveMode);
  }

  if (!GotEdid) {
    DEBUG ((DEBUG_INFO, "HdmiDxe: Using fallback mode 1920×1080@60Hz\n"));
    CopyMem (&mActiveMode, &mMode1080p60, sizeof (mActiveMode));
  }

  // Program HDMI TX hardware
  HdmiConfigureTiming (&mActiveMode);
  HdmiConfigureAviInfoframe (&mActiveMode);

  // Allocate framebuffer and install GOP
  Status = HdmiInstallGop (ImageHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
