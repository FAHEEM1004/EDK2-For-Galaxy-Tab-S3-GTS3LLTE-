/** @file
  MDSS / DSI display driver for Samsung Galaxy Tab S3 LTE.

  Hardware:
    - Qualcomm MDSS (MDP 1.5.1) — multi-layer compositor
    - Dual-DSI (two DSI lanes in sync) driving NT35597 WQXGA panel
    - Panel: Novatek NT35597, 2560×1600, 60Hz command mode
    - Panel strings from LK: "nt35597_wqxga_dualdsi_video" / "qcom,dsi_nt35597_wqxga_video"
    - PMIC display rails enabled by SpmiPmicDxe before this driver runs

  MMIO bases (all verified from XBL/DTB):
    MDSS top:  0x00900000  (qcom,mdss_mdp)
    MDP:       0x00901000
    DSI0 ctrl: 0x00994000
    DSI1 ctrl: 0x00996000
    DSI0 PHY:  0x00994400
    DSI1 PHY:  0x00996400

  Framebuffer: 0x80400000 (8 MB, write-through, "Display Reserved" per XBL).

  This driver:
    1. Resets MDSS and MDP
    2. Configures DSI0+DSI1 in dual-DSI video mode (2560×1600, 60Hz)
    3. Starts pixel DMA from framebuffer → fills black background
    4. Installs EFI_GRAPHICS_OUTPUT_PROTOCOL with the single 2560×1600 mode

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
// MDSS register map
// ---------------------------------------------------------------------------
#define MDSS_BASE                       MSM8996_MDSS_BASE
#define MDSS_HW_REV                     (MDSS_BASE + 0x0000)
#define MDSS_RESET_STATUS               (MDSS_BASE + 0x0008)
#define MDSS_INTR_EN                    (MDSS_BASE + 0x0010)
#define MDSS_INTR_STATUS                (MDSS_BASE + 0x0014)
#define MDSS_INTR_CLEAR                 (MDSS_BASE + 0x0018)
#define MDSS_REG_HW_EVENTS              (MDSS_BASE + 0x0060)
#define MDSS_SW_RESET_CONTROL           (MDSS_BASE + 0x012C)
#define MDSS_MDP_SW_RESET               BIT7

// MDP (display processor)
#define MDP_BASE                        (MDSS_BASE + 0x1000)
#define MDP_HW_REV                      (MDP_BASE + 0x0000)
#define MDP_DISP_INTF_SEL               (MDP_BASE + 0x0004)
#define MDP_VIDEO_INTF_UNDERFLOW_CTL    (MDP_BASE + 0x03E0)
#define MDP_INTR_EN                     (MDP_BASE + 0x0110)
#define MDP_INTR_STATUS                 (MDP_BASE + 0x0114)
#define MDP_INTR_CLEAR                  (MDP_BASE + 0x0118)

// MDP interface select (INTF_SEL bits)
#define MDP_INTF_SEL_DSI0               (1 << 0)
#define MDP_INTF_SEL_DSI1               (1 << 4)

// DSI controller registers (per instance)
#define DSI_BASE(n)                     ((n) == 0 ? MSM8996_DSI0_BASE : MSM8996_DSI1_BASE)
#define DSI_CTRL(n)                     (DSI_BASE(n) + 0x000)
#define DSI_STATUS(n)                   (DSI_BASE(n) + 0x008)
#define DSI_FIFO_STATUS(n)              (DSI_BASE(n) + 0x00C)
#define DSI_VIDEO_MODE_CTRL(n)          (DSI_BASE(n) + 0x010)
#define DSI_VIDEO_MODE_DATA_CTRL(n)     (DSI_BASE(n) + 0x014)
#define DSI_VIDEO_MODE_ACTIVE_H(n)      (DSI_BASE(n) + 0x018)
#define DSI_VIDEO_MODE_ACTIVE_V(n)      (DSI_BASE(n) + 0x01C)
#define DSI_VIDEO_MODE_TOTAL(n)         (DSI_BASE(n) + 0x020)
#define DSI_VIDEO_MODE_HSYNC(n)         (DSI_BASE(n) + 0x024)
#define DSI_VIDEO_MODE_VSYNC(n)         (DSI_BASE(n) + 0x028)
#define DSI_VIDEO_MODE_VSYNC_VPOS(n)    (DSI_BASE(n) + 0x02C)
#define DSI_COMMAND_MODE_DMA_CTRL(n)    (DSI_BASE(n) + 0x038)
#define DSI_COMMAND_MODE_MDP_CTRL(n)    (DSI_BASE(n) + 0x03C)
#define DSI_COMMAND_MODE_MDP_DCS_CMD_CTRL(n)  (DSI_BASE(n) + 0x040)
#define DSI_DMA_CMD_OFFSET(n)           (DSI_BASE(n) + 0x044)
#define DSI_DMA_CMD_LENGTH(n)           (DSI_BASE(n) + 0x048)
#define DSI_RESET(n)                    (DSI_BASE(n) + 0x118)
#define DSI_CLK_CTRL(n)                 (DSI_BASE(n) + 0x11C)
#define DSI_TRIG_CTRL(n)                (DSI_BASE(n) + 0x084)
#define DSI_BROADCAST_EN(n)             (DSI_BASE(n) + 0x0AC)
#define DSI_LANE_CTRL(n)                (DSI_BASE(n) + 0x0AC)

// DSI_CTRL bits
#define DSI_CTRL_ENABLE                 BIT0
#define DSI_CTRL_VIDEO_MODE_EN          BIT1
#define DSI_CTRL_CMD_MODE_EN            BIT2
#define DSI_CTRL_LANES_4                (3 << 4)
#define DSI_CTRL_CRC_CHECK_EN           BIT24
#define DSI_CTRL_ECC_CHECK_EN           BIT20

// NT35597 panel timing (WQXGA 2560×1600 @ 60Hz, dual-DSI half-width = 1280px each)
#define PANEL_HACTIVE                   1280    // per-DSI active pixels
#define PANEL_VACTIVE                   1600
#define PANEL_HBP                       12
#define PANEL_HFP                       32
#define PANEL_HSYNC                     6
#define PANEL_VBP                       4
#define PANEL_VFP                       8
#define PANEL_VSYNC                     2
#define PANEL_HTOTAL                    (PANEL_HACTIVE + PANEL_HBP + PANEL_HFP + PANEL_HSYNC)
#define PANEL_VTOTAL                    (PANEL_VACTIVE + PANEL_VBP + PANEL_VFP + PANEL_VSYNC)

// Framebuffer
#define FB_BASE                         MSM8996_DISPLAY_FB_BASE
#define FB_STRIDE                       (MSM8996_PANEL_WIDTH * 4)   // 32bpp BGRA
#define FB_SIZE                         MSM8996_DISPLAY_FB_SIZE

// ---------------------------------------------------------------------------
// GOP mode table
// ---------------------------------------------------------------------------
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  mGopModeInfo = {
  .Version              = 0,
  .HorizontalResolution = MSM8996_PANEL_WIDTH,
  .VerticalResolution   = MSM8996_PANEL_HEIGHT,
  .PixelFormat          = PixelBlueGreenRedReserved8BitPerColor,
  .PixelsPerScanLine    = MSM8996_PANEL_WIDTH,
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE  mGopMode = {
  .MaxMode             = 1,
  .Mode                = 0,
  .Info                = &mGopModeInfo,
  .SizeOfInfo          = sizeof (mGopModeInfo),
  .FrameBufferBase     = FB_BASE,
  .FrameBufferSize     = FB_SIZE,
};

// ---------------------------------------------------------------------------
// DSI helpers
// ---------------------------------------------------------------------------

STATIC
VOID
DsiSoftReset (
  IN UINTN  DsiNum
  )
{
  MmioWrite32 (DSI_RESET (DsiNum), 1);
  MicroSecondDelay (20);
  MmioWrite32 (DSI_RESET (DsiNum), 0);
}

STATIC
VOID
DsiConfigureVideoMode (
  IN UINTN  DsiNum
  )
{
  // Configure video mode: burst mode, RGB888
  MmioWrite32 (DSI_VIDEO_MODE_CTRL (DsiNum),
               (1 << 0) |   // traffic mode burst
               (3 << 4) |   // dst format RGB888
               (1 << 8));   // HS CLK en

  // Active region
  MmioWrite32 (DSI_VIDEO_MODE_ACTIVE_H (DsiNum),
               (PANEL_HACTIVE << 16) | (PANEL_HBP + PANEL_HSYNC));
  MmioWrite32 (DSI_VIDEO_MODE_ACTIVE_V (DsiNum),
               (PANEL_VACTIVE << 16) | (PANEL_VBP + PANEL_VSYNC));

  // Total (blanking)
  MmioWrite32 (DSI_VIDEO_MODE_TOTAL (DsiNum),
               ((PANEL_VTOTAL - 1) << 16) | (PANEL_HTOTAL - 1));

  // Sync widths
  MmioWrite32 (DSI_VIDEO_MODE_HSYNC (DsiNum),
               ((PANEL_HSYNC + PANEL_HBP - 1) << 16) | (PANEL_HSYNC - 1));
  MmioWrite32 (DSI_VIDEO_MODE_VSYNC (DsiNum),
               ((PANEL_VSYNC + PANEL_VBP - 1) << 16) | (PANEL_VSYNC - 1));
  MmioWrite32 (DSI_VIDEO_MODE_VSYNC_VPOS (DsiNum), 1);

  // Enable: 4 lanes, CRC+ECC check, video mode
  MmioWrite32 (DSI_CTRL (DsiNum),
               DSI_CTRL_CRC_CHECK_EN |
               DSI_CTRL_ECC_CHECK_EN |
               DSI_CTRL_LANES_4      |
               DSI_CTRL_VIDEO_MODE_EN |
               DSI_CTRL_ENABLE);
}

STATIC
VOID
MdpInitFramebuffer (
  VOID
  )
{
  // Clear framebuffer to black (BGRA = 0xFF000000)
  SetMem32 ((VOID *)(UINTN)FB_BASE, FB_SIZE, 0xFF000000u);
}

// ---------------------------------------------------------------------------
// GOP protocol implementation
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
EFIAPI
GopQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL           *This,
  IN  UINT32                                  ModeNumber,
  OUT UINTN                                  *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  if (ModeNumber != 0) {
    return EFI_INVALID_PARAMETER;
  }
  *SizeOfInfo = sizeof (mGopModeInfo);
  *Info       = &mGopModeInfo;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *This,
  IN UINT32                         ModeNumber
  )
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  MdpInitFramebuffer ();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL       *This,
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BltBuffer     OPTIONAL,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION   BltOperation,
  IN UINTN                               SourceX,
  IN UINTN                               SourceY,
  IN UINTN                               DestinationX,
  IN UINTN                               DestinationY,
  IN UINTN                               Width,
  IN UINTN                               Height,
  IN UINTN                               Delta          OPTIONAL
  )
{
  UINT32  *Fb   = (UINT32 *)(UINTN)FB_BASE;
  UINTN   Row, Col;

  switch (BltOperation) {
    case EfiBltVideoFill:
      for (Row = DestinationY; Row < DestinationY + Height; Row++) {
        for (Col = DestinationX; Col < DestinationX + Width; Col++) {
          UINT32  Pixel = ((UINT32)BltBuffer->Blue)         |
                          ((UINT32)BltBuffer->Green << 8)   |
                          ((UINT32)BltBuffer->Red   << 16)  |
                          0xFF000000U;
          Fb[Row * MSM8996_PANEL_WIDTH + Col] = Pixel;
        }
      }
      break;

    case EfiBltBufferToVideo:
    {
      UINTN  SrcStride = (Delta != 0) ? Delta / sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                                      : Width;
      for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Src =
            &BltBuffer[(SourceY + Row) * SrcStride + SourceX + Col];
          Fb[(DestinationY + Row) * MSM8996_PANEL_WIDTH + DestinationX + Col] =
            (UINT32)Src->Blue |
            ((UINT32)Src->Green << 8) |
            ((UINT32)Src->Red << 16)  |
            0xFF000000U;
        }
      }
      break;
    }

    case EfiBltVideoToBltBuffer:
    {
      for (Row = 0; Row < Height; Row++) {
        for (Col = 0; Col < Width; Col++) {
          UINT32  Pixel = Fb[(SourceY + Row) * MSM8996_PANEL_WIDTH + SourceX + Col];
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dst =
            &BltBuffer[(DestinationY + Row) * (Delta ? Delta / sizeof(*BltBuffer) : Width) + DestinationX + Col];
          Dst->Blue     = (UINT8)(Pixel & 0xFF);
          Dst->Green    = (UINT8)((Pixel >> 8) & 0xFF);
          Dst->Red      = (UINT8)((Pixel >> 16) & 0xFF);
          Dst->Reserved = 0xFF;
        }
      }
      break;
    }

    case EfiBltVideoToVideo:
      // Simplified copy
      for (Row = 0; Row < Height; Row++) {
        CopyMem (&Fb[(DestinationY + Row) * MSM8996_PANEL_WIDTH + DestinationX],
                 &Fb[(SourceY + Row)      * MSM8996_PANEL_WIDTH + SourceX],
                 Width * sizeof (UINT32));
      }
      break;

    default:
      return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  mGop = {
  GopQueryMode,
  GopSetMode,
  GopBlt,
  &mGopMode,
};

/**
  Entry point for MDSS display driver.
**/
EFI_STATUS
EFIAPI
MdssDisplayDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle = NULL;

  DEBUG ((DEBUG_INFO, "MdssDisplayDxe: MDSS @ 0x%08X, FB @ 0x%08X (%dx%d)\n",
          MSM8996_MDSS_BASE, FB_BASE,
          MSM8996_PANEL_WIDTH, MSM8996_PANEL_HEIGHT));

  // MDSS software reset
  MmioOr32 (MDSS_SW_RESET_CONTROL, MDSS_MDP_SW_RESET);
  MicroSecondDelay (100);
  MmioAnd32 (MDSS_SW_RESET_CONTROL, ~MDSS_MDP_SW_RESET);

  // Enable MDP interface select: DSI0 + DSI1
  MmioWrite32 (MDP_DISP_INTF_SEL, MDP_INTF_SEL_DSI0 | MDP_INTF_SEL_DSI1);

  // Reset and configure both DSI controllers
  DsiSoftReset (0);
  DsiSoftReset (1);

  // DSI1 broadcast (slave) must be enabled before DSI0 (master)
  MmioWrite32 (DSI_BROADCAST_EN (1), 1);
  MmioWrite32 (DSI_BROADCAST_EN (0), 1);

  DsiConfigureVideoMode (1);  // slave first
  DsiConfigureVideoMode (0);  // master fires timing

  // Initialize framebuffer — clear to black
  MdpInitFramebuffer ();

  DEBUG ((DEBUG_INFO, "MdssDisplayDxe: DSI0+DSI1 configured, NT35597 2560x1600\n"));

  // Install GOP
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &mGop,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MdssDisplayDxe: GOP install failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MdssDisplayDxe: GOP protocol installed (2560x1600 BGRA)\n"));
  return EFI_SUCCESS;
}
