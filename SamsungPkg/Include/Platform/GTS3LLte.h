/** @file
  Hardware base addresses and constants for Samsung Galaxy Tab S3 LTE
  (SM-T825 / GTS3L LTE) — Qualcomm MSM8996 (Snapdragon 820).

  ALL addresses verified against:
    - XBL.elf   (eXtensible BootLoader) memory/MMIO map
    - LK appsboot.mbn   (Little Kernel, device drivers)
    - RPM.mbn   (Resource Power Manager)
    - TZ.mbn    (TrustZone, reserved-region table)
    - HYP.mbn   (Hypervisor load address)
    - DTB from boot.img (device-tree node reg values)
    - PIT       (Samsung partition table)

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GTS3L_LTE_H_
#define GTS3L_LTE_H_

// ---------------------------------------------------------------------------
// SoC identification
// ---------------------------------------------------------------------------
#define MSM8996_SOC_ID                  0xF6
#define MSM8996_SOC_REV                 0x30001

// ---------------------------------------------------------------------------
// CPU — 4x Qualcomm Kryo (AArch64), 2 clusters, PSCI 1.0 / SMC
//   Cluster 0 (efficiency): cpu@0, cpu@1
//   Cluster 1 (performance): cpu@100, cpu@101
// ---------------------------------------------------------------------------
#define MSM8996_CLUSTER_COUNT           2
#define MSM8996_CORE_COUNT              4

// ---------------------------------------------------------------------------
// MMIO map — verified from XBL device-region table
// ---------------------------------------------------------------------------

// PCIE PHY — three separate QMP PHY instances, contiguous, 0x4000 bytes each
// XBL: device-region table shows block at 0x00034000 size 0x0000C000
#define MSM8996_PCIE_PHY_BASE           0x00034000  // PCIe0 QMP PHY
#define MSM8996_PCIE1_PHY_BASE          0x00038000  // PCIe1 QMP PHY
#define MSM8996_PCIE2_PHY_BASE          0x0003C000  // PCIe2 QMP PHY
#define MSM8996_PCIE_PHY_SIZE           0x00004000  // per-PHY size

// RPM Message RAM
#define MSM8996_RPM_MSG_RAM_BASE        0x00068000
#define MSM8996_RPM_MSG_RAM_SIZE        0x00008000

// Security Control (fuses, QFPROM)
#define MSM8996_SECURITY_CTRL_BASE      0x00070000
#define MSM8996_SECURITY_CTRL_SIZE      0x00010000

// PRNG (random number generator)
#define MSM8996_PRNG_BASE               0x00080000
#define MSM8996_PRNG_SIZE               0x00010000

// GCC Global Clock Controller
#define MSM8996_GCC_BASE                0x00300000
#define MSM8996_GCC_SIZE                0x000A0000

// MPM2 (Mobile Power Management)
#define MSM8996_MPM2_BASE               0x004A0000
#define MSM8996_MPM2_SIZE               0x00040000

// PCIe Wrapper AHB (config space)
#define MSM8996_PCIE_AHB_BASE           0x00600000
#define MSM8996_PCIE_AHB_SIZE           0x00018000
// PCIe controllers on PCIe bus
#define MSM8996_PCIE0_BASE              0x00600000
#define MSM8996_PCIE1_BASE              0x00608000
#define MSM8996_PCIE2_BASE              0x00610000

// UFS (Universal Flash Storage) — jedec,ufs-1.1
// Verified from XBL: "UFS UFS REGS" @ 0x620000
#define MSM8996_UFS_BASE                0x00620000
#define MSM8996_UFS_SIZE                0x00020000

// Crypto engine
#define MSM8996_CRYPTO_BASE             0x00640000
#define MSM8996_CRYPTO_SIZE             0x00040000

// CORE_TOP_CSR (security / XPU configuration)
#define MSM8996_CORE_TOP_CSR_BASE       0x00700000
#define MSM8996_CORE_TOP_CSR_SIZE       0x00100000

// MMSS — Multimedia SubSystem (display, DSI, HDMI, camera, GPU)
// DTB: qcom,mdss_mdp@900000  reg = <0x900000 0x90000 ...>
#define MSM8996_MMSS_BASE               0x00800000
#define MSM8996_MMSS_SIZE               0x00800000
#define MSM8996_MDSS_BASE               0x00900000
#define MSM8996_MDSS_SIZE               0x00090000
#define MSM8996_DSI0_BASE               0x00994000
#define MSM8996_DSI0_SIZE               0x00000400
#define MSM8996_DSI1_BASE               0x00996000
#define MSM8996_DSI1_SIZE               0x00000400
#define MSM8996_HDMI_BASE               0x009A0000
#define MSM8996_HDMI_SIZE               0x0000050C

// TLMM — Top Level Mode Multiplexer (GPIO controller)
// DTB: tlmm / XBL: "TLMM CSR"
#define MSM8996_TLMM_BASE               0x01000000
#define MSM8996_TLMM_SIZE               0x00310000
#define MSM8996_GPIO_COUNT              150

// SPMI / PMIC Arbiter  — PM8994 + PMI8994
// XBL: "PMIC ARB SPMI" @ 0x04000000
#define MSM8996_SPMI_BASE               0x04000000
#define MSM8996_SPMI_SIZE               0x01C00000

// HMSS_QLL — XBL code region (Qualcomm Bootrom region)
#define MSM8996_HMSS_QLL_BASE           0x06400000
#define MSM8996_HMSS_QLL_SIZE           0x00200000

// IMEM (Internal SRAM)
#define MSM8996_IMEM_BASE               0x06680000
#define MSM8996_IMEM_SIZE               0x00040000
#define MSM8996_IMEM_COOKIE_BASE        0x066BF000  // boot cookie / SMEM init

// USB 3.0 — qcom,dwc-usb3-msm
// XBL: "USB30_PRIM" @ 0x06A00000
#define MSM8996_USB3_BASE               0x06A00000
#define MSM8996_USB3_SIZE               0x00200000
#define MSM8996_QUSB2_PHY_BASE          0x07411000  // QUSB2 HS PHY (within BLSP block 0x07400000)
#define MSM8996_USB3_QMP_PHY_BASE       0x07410000  // SS PHY (QMP v2, within BLSP block)

// COPSS_ISTARI — BLSP buses (UART, I2C, SPI)
#define MSM8996_BLSP_BASE               0x07400000
#define MSM8996_BLSP_SIZE               0x00400000

// BLSP1 UARTs / QUP
#define MSM8996_BLSP1_UART0_BASE        0x07570000  // serial@07570000  (lsuart, disabled)
#define MSM8996_BLSP1_UART1_BASE        0x07571000  // uart@7571000     (hsuart)
#define MSM8996_BLSP1_UART2_BASE        0x07574000  // uart@7574000     (hsuart)
// BLSP2 UARTs
#define MSM8996_BLSP2_UART0_BASE        0x075B0000  // serial@075b0000  (lsuart, STATUS=OK — debug)
#define MSM8996_BLSP2_UART1_BASE        0x075AF000  // uart@075AF000    (hsuart, disabled)
#define MSM8996_UART_SIZE               0x00001000

// Debug UART — confirmed active by DTB (status="ok") and LK ("UART_DM Initialized")
#define MSM8996_DEBUG_UART_BASE         MSM8996_BLSP2_UART0_BASE
#define MSM8996_DEBUG_UART_IRQ          0x72        // SPI 114

// UART register offsets (MSM UARTDM v1.4 non-DM path)
#define UART_MR1                        0x000
#define UART_MR2                        0x004
#define UART_IMR                        0x008   // write
#define UART_SR                         0x008   // read  — status
#define UART_CR                         0x010   // write — command
#define UART_ISR                        0x010   // read
#define UART_IPR                        0x018
#define UART_TFWR                       0x01C
#define UART_RFWR                       0x020
#define UART_HCR                        0x024
#define UART_MREG                       0x028
#define UART_NREG                       0x02C
#define UART_DREG                       0x030
#define UART_MNDREG                     0x034
#define UART_IRDA                       0x038
#define UART_DMEN                       0x03C
#define UART_NO_CHARS_FOR_TX            0x040
#define UART_BADR                       0x0A4
#define UART_TF                         0x100   // TX FIFO
#define UART_RF                         0x140   // RX FIFO
// UART_SR bits
#define UART_SR_RXRDY                   BIT0
#define UART_SR_RXFULL                  BIT1
#define UART_SR_TXRDY                   BIT2    // TX FIFO has space
#define UART_SR_TXEMT                   BIT3    // TX shift register empty
#define UART_SR_OVRUN                   BIT4
#define UART_SR_PAR_FRAME_ERR           BIT5
#define UART_SR_RX_BREAK                BIT6

// BLSP I2C buses (from DTB aliases)
#define MSM8996_I2C1_BASE               0x075BA000  // i2c1 = i2c@75ba000
#define MSM8996_I2C2_BASE               0x07576000
#define MSM8996_I2C3_BASE               0x07577000
#define MSM8996_I2C5_BASE               0x07579000
#define MSM8996_I2C7_BASE               0x075B5000
#define MSM8996_I2C8_BASE               0x075B6000
#define MSM8996_I2C9_BASE               0x075B7000
#define MSM8996_I2C12_BASE              0x075BA000

// BLSP SPI buses (from DTB aliases)
#define MSM8996_SPI0_BASE               0x07575000  // spi0
#define MSM8996_SPI6_BASE               0x0757A000
#define MSM8996_SPI9_BASE               0x075B7000
#define MSM8996_SPI10_BASE              0x075B8000
#define MSM8996_SPI11_BASE              0x075B9000
#define MSM8996_SPI12_BASE              0x075BA000

// HMSS — CPU subsystem, GIC
// XBL: "HMSS" @ 0x09800000, size 0x800000
#define MSM8996_HMSS_BASE               0x09800000
#define MSM8996_HMSS_SIZE               0x00800000

// GICv3 — verified from DTB: interrupt-controller@09bc0000
#define MSM8996_GIC_DIST_BASE           0x09BC0000  // GICD
#define MSM8996_GIC_DIST_SIZE           0x00010000
#define MSM8996_GIC_REDIST_BASE         0x09C00000  // GICR (redistributors)
#define MSM8996_GIC_REDIST_SIZE         0x00100000
#define MSM8996_GIC_ITS_BASE            0x09BE0000  // GIC-ITS (message-based IRQ)
#define MSM8996_GIC_ITS_SIZE            0x00020000

// PCIe Wrapper AXI (memory window)
#define MSM8996_PCIE_AXI_BASE           0x0C000000
#define MSM8996_PCIE_AXI_SIZE           0x04000000

// SDHC (microSD slot 2)  — sdhci@74A4900
#define MSM8996_SDHC2_BASE              0x074A4900
#define MSM8996_SDHC2_SIZE             0x00001000

// ARM Generic Timer — clock-frequency = 0x124f800 = 19,200,000 Hz (19.2 MHz TCXO)
#define MSM8996_TIMER_FREQ_HZ           19200000

// PSHOLD reset — qcom,pshold @ 0x4ab000 (DTB + LK confirmed)
#define MSM8996_PSHOLD_BASE             0x004AB000
#define MSM8996_TCSR_BOOT_MISC          0x007B3000

// Watchdog — qcom,msm-watchdog
#define MSM8996_WDOG_BASE               0x00980000

// ---------------------------------------------------------------------------
// DRAM layout — verified from XBL device-region table (AUTHORITATIVE)
//
//  Physical DRAM: 0x80000000 – (0x80000000 + 4GB)
//
//  XBL-managed regions (must NOT be disturbed by UEFI):
//    0x80100000 0x00100000  HLOS 0           (Conv/WB)
//    0x80200000 0x00400000  UEFI FD          ← firmware image loads here
//    0x80300000 0x00080000  MPPark Code      (RT code, uncached)
//    0x80380000 0x00001000  FBPT Payload     (RT data)
//    0x80381000 0x00004000  DBG2             (loader data)
//    0x80385000 0x00001000  Capsule Header   (RT data)
//    0x80386000 0x00003000  TPM Control Area (RT data)
//    0x80389000 0x00001000  UEFI Info Block  (RT data)
//    0x8038A000 0x00003000  Reset Data       (RT data)
//    0x8038D000 0x00073000  Rsvd Uncached 0  (BS data, UC)
//    0x80400000 0x00800000  Display Reserved (framebuffer, WT)
//    0x80C00000 0x00040000  UEFI Stack       (BS data, WB)
//    0x80C40000 0x00010000  CPU Vectors      (BS data, WB)
//    0x80C50000 0x000B0000  Rsvd Cached 0    (BS data, WB)
//    0x80D00000 0x03300000  HLOS 1           (BS data, WB)
//    0x84000000 0x01800000  HLOS 2           (BS data, WB)
//    0x85800000 0x00600000  HYP              (Hypervisor, NS)
//    0x85E00000 0x00180000  MPSS_EFS         (reserved, NS)
//    0x85F80000 0x00020000  ADSP_EFS         (reserved, NS)
//    0x85FA0000 0x00060000  HLOS 3           (Conv, WB)
//    0x86000000 0x00200000  SMEM             (shared mem, UC)
//    0x86200000 0x00100000  TZ_STAT          (reserved, NS)
//    0x86300000 0x00100000  TAGS             (reserved, NS)
//    0x86400000 0x00200000  TZ               (TrustZone, NS)
//    0x86600000 0x00E00000  TZ Apps          (reserved, NS)
//    0x87400000 0x01400000  Stargate         (reserved, NS)
//    0x88800000 0x08700000  PIL_REGION       (Modem+ADSP, NS)
//    0x90F00000 0x00300000  CNSS_DEBUG       (WiFi QCA6174, NS)
//    0x91200000 0x0EE00000  HLOS 4           (Conv, WB)
//    0xA0000000  remainder  HLOS 5           (Conv, WB)
// ---------------------------------------------------------------------------
#define MSM8996_DRAM_BASE               0x80000000ULL
#define MSM8996_DRAM_SIZE               0x100000000ULL   // 4 GB

// UEFI firmware image (FD) — XBL loads UEFI here
#define MSM8996_FD_BASE                 0x80200000ULL
#define MSM8996_FD_SIZE                 0x00400000       // 4 MB
#define MSM8996_FD_BLOCK_SIZE           0x00001000       // 4 KB page

// Display framebuffer — XBL "Display Reserved"
#define MSM8996_DISPLAY_FB_BASE         0x80400000
#define MSM8996_DISPLAY_FB_SIZE         0x00800000       // 8 MB

// UEFI Stack — XBL "UEFI Stack"
#define MSM8996_UEFI_STACK_BASE         0x80C00000
#define MSM8996_UEFI_STACK_SIZE         0x00040000       // 256 KB

// CPU exception vectors
#define MSM8996_CPU_VECTORS_BASE        0x80C40000
#define MSM8996_CPU_VECTORS_SIZE        0x00010000

// SMEM (Shared Memory)
#define MSM8996_SMEM_BASE               0x86000000
#define MSM8996_SMEM_SIZE               0x00200000

// TrustZone
#define MSM8996_TZ_BASE                 0x86400000
#define MSM8996_TZ_SIZE                 0x00200000

// Hypervisor
#define MSM8996_HYP_BASE                0x85800000
#define MSM8996_HYP_SIZE                0x00600000

// PIL region (Modem + ADSP + peripheral firmware)
#define MSM8996_PIL_BASE                0x88800000
#define MSM8996_PIL_SIZE                0x08700000

// CNSS (WiFi QCA6174 debug)
#define MSM8996_CNSS_BASE               0x90F00000
#define MSM8996_CNSS_SIZE               0x00300000

// Free conventional DRAM for DXE (HLOS 1 + 2, between FD area and reserved)
// HLOS 1: 0x80D00000 – 0x83FFFFFF (51 MB)
// HLOS 2: 0x84000000 – 0x857FFFFF (24 MB)
// = 75 MB of unreserved DRAM for DXE + drivers
#define MSM8996_UEFI_RAM_BASE           0x80D00000
#define MSM8996_UEFI_RAM_SIZE           (0x84000000 + 0x01800000 - 0x80D00000)

// Panel
#define MSM8996_PANEL_WIDTH             2560
#define MSM8996_PANEL_HEIGHT            1600
#define MSM8996_PANEL_BPP               24

// PSCI (arm,psci-1.0, SMC method)
#define MSM8996_PSCI_VER                0x10000
#define MSM8996_PSCI_CPU_ON             0x80000003
#define MSM8996_PSCI_AFFINITY_INFO      0x80000004
#define MSM8996_PSCI_SYSTEM_OFF         0x84000008
#define MSM8996_PSCI_SYSTEM_RESET       0x84000009

// ---------------------------------------------------------------------------
// PMIC — PM8994 (main) + PMI8994 (companion, fuel gauge / charger)
//   Accessed via SPMI bus (0x04000000)
//   Charger IC: Samsung SM5705
//   Fuel Gauge: SM5705 (separate I2C)
// ---------------------------------------------------------------------------
#define MSM8996_PM8994_SLAVEADDR        0x0   // SPMI slave 0
#define MSM8996_PMI8994_SLAVEADDR       0x2   // SPMI slave 2

// ---------------------------------------------------------------------------
// UFS partition offsets (from PIT, in 512-byte sectors converted from KB)
// Sizes are in KB as read from PIT.
// ---------------------------------------------------------------------------
#define UFS_PART_XBL_START_KB           32768
#define UFS_PART_XBL_SIZE_KB            24576
#define UFS_PART_ABOOT_START_KB         65536
#define UFS_PART_ABOOT_SIZE_KB          163840
#define UFS_PART_RPM_START_KB           229408
#define UFS_PART_TZ_START_KB            233504
#define UFS_PART_HYP_START_KB           249888
#define UFS_PART_PMIC_START_KB          253984
#define UFS_PART_BOOT_START_KB          942080
#define UFS_PART_BOOT_SIZE_KB           622592
#define UFS_PART_RECOVERY_START_KB      1564672
#define UFS_PART_SYSTEM_START_KB        4653056
#define UFS_PART_CACHE_START_KB         37421056
#define UFS_PART_USERDATA_START_KB      39387136

#endif // GTS3L_LTE_H_
