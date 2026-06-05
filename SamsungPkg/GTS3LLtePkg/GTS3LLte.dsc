## @file
#  Platform Description File for Samsung Galaxy Tab S3 LTE (GTS3L LTE, SM-T825)
#  SoC: Qualcomm MSM8996 (Snapdragon 820) — AArch64
#
#  Hardware features covered by this port:
#    - 4x Qualcomm Kryo CPU (AArch64, PSCI 1.0)
#    - ARM GICv3 @ 0x09BC0000 / 0x09C00000
#    - ARM Generic Timer @ 19.2 MHz
#    - MSM UART (lsuart-v14) @ 0x075B0000  — debug serial
#    - UFS 1.1 internal storage
#    - USB 3.0 (DWC3)
#    - DSI display (Sharp WQXGA 2560x1600, dual-DSI)
#    - HDMI (Qualcomm HDMI TX)
#    - PCIe x1 × 3 (WiFi QCA6174 on PCIe0)
#    - Bluetooth QCA6174
#    - SM5705 charger + fuel gauge
#    - SPMI PMIC (PM8994 / PMI8994)
#    - PSHOLD reset @ 0x004AB000
#
#  Copyright (c) 2024, Samsung / TianoCore Contributors.
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = GTS3LLte
  PLATFORM_GUID                  = 5A3C1F8D-7B2E-4E9A-C012-4D6A0E1B3C78
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x0001001B
  OUTPUT_DIRECTORY               = Build/GTS3LLte
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

  FLASH_DEFINITION               = SamsungPkg/GTS3LLtePkg/GTS3LLte.fdf

  #
  # UEFI FD base — XBL loads firmware HERE (verified from XBL "UEFI FD" region table)
  #
  DEFINE FD_BASE                 = 0x80200000
  DEFINE FD_SIZE                 = 0x00400000  # 4 MB
  DEFINE FD_BLOCK_SIZE           = 0x00001000  # 4 KB pages

  # Free DRAM for DXE (HLOS1 0x80D00000–0x83FFFFFF = 51 MB
  #                   + HLOS2 0x84000000–0x857FFFFF = 24 MB = 75 MB total)
  DEFINE SYSTEM_RAM_BASE         = 0x80D00000
  DEFINE SYSTEM_RAM_SIZE         = 0x04B00000   # 75 MB

# ---------------------------------------------------------------------------
# PCDs
# ---------------------------------------------------------------------------

[PcdsFixedAtBuild.common]
  # Platform identity
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareVendor|L"Samsung GTS3L LTE (MSM8996)"
  gEfiMdeModulePkgTokenSpaceGuid.PcdFirmwareRevision|0x00000001

  # Memory
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x100000000
  gSamsungTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gSamsungTokenSpaceGuid.PcdSystemMemorySize|0x100000000

  # Firmware image location
  gArmTokenSpaceGuid.PcdFvBaseAddress|$(FD_BASE)
  gArmTokenSpaceGuid.PcdFvSize|$(FD_SIZE)
  gSamsungTokenSpaceGuid.PcdFdBaseAddress|$(FD_BASE)
  gSamsungTokenSpaceGuid.PcdFdSize|$(FD_SIZE)

  # GICv3
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x09C00000
  gSamsungTokenSpaceGuid.PcdGicDistributorBase|0x09BC0000
  gSamsungTokenSpaceGuid.PcdGicRedistributorsBase|0x09C00000

  # ARM Generic Timer — 19.2 MHz TCXO
  gEmbeddedTokenSpaceGuid.PcdTimerPeriod|100000          # 10 ms tick
  gSamsungTokenSpaceGuid.PcdArmArchTimerFreqInHz|19200000

  # PSCI — SMC-based, PSCI 1.0
  gArmPlatformTokenSpaceGuid.PcdCoreCount|4
  gArmPlatformTokenSpaceGuid.PcdClusterCount|2

  # UART serial port — serial@075b0000 (msm-lsuart-v14, status=ok)
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x075B0000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseMmio|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialBaudRate|115200
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialClockRate|7372800
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterStride|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gSamsungTokenSpaceGuid.PcdMsmUartBase|0x075B0000

  # PSHOLD reset
  gSamsungTokenSpaceGuid.PcdPsHoldBase|0x004AB000

  # Display — Novatek NT35597 WQXGA dual-DSI (2560 × 1600, 60Hz)
  # Panel confirmed from LK strings: "nt35597_wqxga_dualdsi_video"
  gSamsungTokenSpaceGuid.PcdDisplayWidth|2560
  gSamsungTokenSpaceGuid.PcdDisplayHeight|1600
  gSamsungTokenSpaceGuid.PcdFrameBufferBase|0x80400000
  gSamsungTokenSpaceGuid.PcdFrameBufferSize|0x00800000
  gSamsungTokenSpaceGuid.PcdMdssBase|0x00900000
  gSamsungTokenSpaceGuid.PcdDsi0Base|0x00994000
  gSamsungTokenSpaceGuid.PcdDsi1Base|0x00996000
  gSamsungTokenSpaceGuid.PcdHdmiBase|0x009A0000

  # UFS + USB + PCIe + SPMI
  gSamsungTokenSpaceGuid.PcdUfsBase|0x00620000
  gSamsungTokenSpaceGuid.PcdUsb3Base|0x06A00000
  gSamsungTokenSpaceGuid.PcdSpmiBase|0x04000000
  gSamsungTokenSpaceGuid.PcdPcie0AhbBase|0x00600000
  gSamsungTokenSpaceGuid.PcdPcie0AxiBase|0x0C000000

  # GCC + TLMM + IMEM
  gSamsungTokenSpaceGuid.PcdGccBase|0x00300000
  gSamsungTokenSpaceGuid.PcdTlmmBase|0x01000000
  gSamsungTokenSpaceGuid.PcdGpioCount|150
  gSamsungTokenSpaceGuid.PcdImemBase|0x06680000
  gSamsungTokenSpaceGuid.PcdImemCookieBase|0x066BF000

  # SM5705 I2C
  gSamsungTokenSpaceGuid.PcdSm5705I2cBase|0x075B6000

  # SMEM
  gSamsungTokenSpaceGuid.PcdSmemBase|0x86000000
  gSamsungTokenSpaceGuid.PcdSmemSize|0x00200000

  # UEFI Stack
  gSamsungTokenSpaceGuid.PcdUefiStackBase|0x80C00000
  gSamsungTokenSpaceGuid.PcdUefiStackSize|0x00040000

  # Stack settings — XBL "UEFI Stack" region @ 0x80C00000, 256 KB
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x80C00000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000   # 256 KB

  # Debug output
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8000004F
  gEfiMdePkgTokenSpaceGuid.PcdReportStatusCodePropertyMask|0x07

  # ACPI / SMBIOS
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemId|"SAMSNG"
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultOemTableId|0x334C5354474C5347  # "GTS3LLTE"
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultCreatorId|0x20544449           # "IDT "
  gEfiMdeModulePkgTokenSpaceGuid.PcdAcpiDefaultCreatorRevision|0x00000001

  # eMMC-style mass storage (we use UFS)
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE

[PcdsPatchableInModule.common]
  # Console
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutRow|50
  gEfiMdeModulePkgTokenSpaceGuid.PcdConOutColumn|160

# ---------------------------------------------------------------------------
# Library class mappings
# ---------------------------------------------------------------------------

[LibraryClasses.common]
  # Entry points
  DxeCoreEntryPoint|MdePkg/Library/DxeCoreEntryPoint/DxeCoreEntryPoint.inf
  UefiDriverEntryPoint|MdePkg/Library/UefiDriverEntryPoint/UefiDriverEntryPoint.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf

  # Compiler / linker support
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  CpuLib|MdePkg/Library/BaseCpuLib/BaseCpuLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsicArmVirt.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  ExtractGuidedSectionLib|EmbeddedPkg/Library/PrePiExtractGuidedSectionLib/PrePiExtractGuidedSectionLib.inf
  PrePiHobListPointerLib|ArmPlatformPkg/Library/PrePiHobListPointerLib/PrePiHobListPointerLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  ReportStatusCodeLib|MdePkg/Library/BaseReportStatusCodeLibNull/BaseReportStatusCodeLibNull.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLib.inf
  VariablePolicyHelperLib|MdeModulePkg/Library/VariablePolicyHelperLib/VariablePolicyHelperLib.inf
  TpmMeasurementLib|MdeModulePkg/Library/TpmMeasurementLibNull/TpmMeasurementLibNull.inf
  AuthVariableLib|MdeModulePkg/Library/AuthVariableLibNull/AuthVariableLibNull.inf
  VarCheckLib|MdeModulePkg/Library/VarCheckLib/VarCheckLib.inf
  VariableFlashInfoLib|MdeModulePkg/Library/BaseVariableFlashInfoLib/BaseVariableFlashInfoLib.inf
  MmUnblockMemoryLib|MdePkg/Library/MmUnblockMemoryLib/MmUnblockMemoryLibNull.inf
  SecurityManagementLib|MdeModulePkg/Library/DxeSecurityManagementLib/DxeSecurityManagementLib.inf
  RngLib|MdePkg/Library/DxeRngLib/DxeRngLib.inf
  ArmTrngLib|MdePkg/Library/BaseArmTrngLibNull/BaseArmTrngLibNull.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiRuntimeLib|MdePkg/Library/UefiRuntimeLib/UefiRuntimeLib.inf
  HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
  SynchronizationLib|MdePkg/Library/BaseSynchronizationLib/BaseSynchronizationLib.inf
  PerformanceLib|MdePkg/Library/BasePerformanceLibNull/BasePerformanceLibNull.inf
  OrderedCollectionLib|MdePkg/Library/BaseOrderedCollectionRedBlackTreeLib/BaseOrderedCollectionRedBlackTreeLib.inf
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf

  # ARM
  ArmLib|MdePkg/Library/ArmLib/ArmBaseLib.inf
  ArmMmuLib|UefiCpuPkg/Library/ArmMmuLib/ArmMmuBaseLib.inf
  ArmSmcLib|MdePkg/Library/ArmSmcLib/ArmSmcLib.inf
  ArmHvcLib|ArmPkg/Library/ArmHvcLib/ArmHvcLib.inf
  ArmMonitorLib|ArmPkg/Library/ArmMonitorLib/ArmMonitorLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  ArmPlatformLib|SamsungPkg/GTS3LLtePkg/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  CpuExceptionHandlerLib|ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.inf

  # Reset / PSCI
  ArmPsciResetSystemLib|ArmPkg/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf
  ResetSystemLib|ArmPkg/Library/ArmPsciResetSystemLib/ArmPsciResetSystemLib.inf

  # Serial / UART
  SerialPortLib|SamsungPkg/GTS3LLtePkg/Library/MsmUartSerialPortLib/MsmUartSerialPortLib.inf

  # Boot manager
  PlatformBootManagerLib|SamsungPkg/GTS3LLtePkg/Library/PlatformBootManagerLib/PlatformBootManagerLib.inf

  # Timers
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  RealTimeClockLib|EmbeddedPkg/Library/TemplateRealTimeClockLib/TemplateRealTimeClockLib.inf
  TimeBaseLib|EmbeddedPkg/Library/TimeBaseLib/TimeBaseLib.inf

  # Misc
  DxeServicesTableLib|MdePkg/Library/DxeServicesTableLib/DxeServicesTableLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  UefiBootManagerLib|MdeModulePkg/Library/UefiBootManagerLib/UefiBootManagerLib.inf
  UefiUsbLib|MdePkg/Library/UefiUsbLib/UefiUsbLib.inf
  UefiScsiLib|MdePkg/Library/UefiScsiLib/UefiScsiLib.inf
  BootLogoLib|MdeModulePkg/Library/BootLogoLib/BootLogoLib.inf
  FileExplorerLib|MdeModulePkg/Library/FileExplorerLib/FileExplorerLib.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf
  NonDiscoverableDeviceRegistrationLib|MdeModulePkg/Library/NonDiscoverableDeviceRegistrationLib/NonDiscoverableDeviceRegistrationLib.inf

!include NetworkPkg/NetworkLibs.dsc.inc


[LibraryClasses.common.DXE_CORE]
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf

[LibraryClasses.common.SEC]
  HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
  MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
  PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf

[LibraryClasses.common.DXE_DRIVER]
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  VariablePolicyLib|MdeModulePkg/Library/VariablePolicyLib/VariablePolicyLibRuntimeDxe.inf
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf

[LibraryClasses.common.UEFI_APPLICATION]
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

[LibraryClasses.common.UEFI_DRIVER]
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------

[BuildOptions.common]
  GCC:*_*_AARCH64_CC_FLAGS = -DARM_CPU_AARCH64 -DMSM8996 -DGTS3L_LTE

[BuildOptions.AARCH64.RELEASE]
  GCC:*_*_AARCH64_CC_FLAGS = -DMDEPKG_NDEBUG

# ---------------------------------------------------------------------------
# Component list
# ---------------------------------------------------------------------------

[Components.AARCH64]

  # ------------------------------------------------------------------
  # SEC / Pre-PI
  # ------------------------------------------------------------------
  SamsungPkg/GTS3LLtePkg/PrePi/PrePi.inf {
    <LibraryClasses>
      HobLib|EmbeddedPkg/Library/PrePiHobLib/PrePiHobLib.inf
      MemoryAllocationLib|EmbeddedPkg/Library/PrePiMemoryAllocationLib/PrePiMemoryAllocationLib.inf
      PrePiLib|EmbeddedPkg/Library/PrePiLib/PrePiLib.inf
      PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
      SerialPortLib|SamsungPkg/GTS3LLtePkg/Library/MsmUartSerialPortLib/MsmUartSerialPortLib.inf
  }

  # ------------------------------------------------------------------
  # DXE Core
  # ------------------------------------------------------------------
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
  }

  # ------------------------------------------------------------------
  # PCD database
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  # ------------------------------------------------------------------
  # Core architecture protocols
  # ------------------------------------------------------------------
  ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  ArmPkg/Drivers/TimerDxe/TimerDxe.inf
  ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  ArmPkg/Drivers/ArmPsciMpServicesDxe/ArmPsciMpServicesDxe.inf

  # ------------------------------------------------------------------
  # Platform DXE (SMBIOS, reset, early init)
  # ------------------------------------------------------------------
  SamsungPkg/GTS3LLtePkg/Drivers/PlatformDxe/PlatformDxe.inf

  # ------------------------------------------------------------------
  # Watchdog / timer
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf

  # ------------------------------------------------------------------
  # Runtime services
  # ------------------------------------------------------------------
  MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
      NULL|EmbeddedPkg/Library/NvVarStoreFormattedLib/NvVarStoreFormattedLib.inf
  }
  MdeModulePkg/Universal/MonotonicCounterRuntimeDxe/MonotonicCounterRuntimeDxe.inf
  MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf

  # ------------------------------------------------------------------
  # Hardware platform drivers (load order via DEPEX)
  # ------------------------------------------------------------------

  # 1. GCC clock controller — must come first (gates all other hardware)
  SamsungPkg/GTS3LLtePkg/Drivers/GccClockDxe/GccClockDxe.inf

  # 2. TLMM GPIO — needed by PMIC, Display, Charger (all use GPIOs)
  SamsungPkg/GTS3LLtePkg/Drivers/TlmmGpioDxe/TlmmGpioDxe.inf

  # 3. SPMI / PMIC (PM8994 + PMI8994) — voltage rails for display + storage
  SamsungPkg/GTS3LLtePkg/Drivers/SpmiPmicDxe/SpmiPmicDxe.inf

  # 4. MDSS / DSI display — NT35597 WQXGA dual-DSI (installs GOP)
  SamsungPkg/GTS3LLtePkg/Drivers/MdssDisplayDxe/MdssDisplayDxe.inf

  # 5. HDMI TX — hot-plug + EDID (1080p60 if connected)
  SamsungPkg/GTS3LLtePkg/Drivers/HdmiDxe/HdmiDxe.inf

  # 6. UFS 1.1 internal storage (UFSHCI, block IO for all LUNs)
  SamsungPkg/GTS3LLtePkg/Drivers/UfsDxe/UfsDxe.inf

  # 7. USB 3.0 DWC3 (QUSB2 PHY + QMP SS PHY → xHCI registration)
  SamsungPkg/GTS3LLtePkg/Drivers/Usb3Dwc3Dxe/Usb3Dwc3Dxe.inf

  # 8. PCIe (3× root complexes; PCIe0 = QCA6174 WiFi/BT)
  SamsungPkg/GTS3LLtePkg/Drivers/PcieDxe/PcieDxe.inf

  # 9. SM5705 charger + MUIC + fuel gauge (I2C via BLSP2 QUP2)
  SamsungPkg/GTS3LLtePkg/Drivers/Sm5705ChargerDxe/Sm5705ChargerDxe.inf

  # 10. ACPI tables (MADT/GICv3, GTDT, SPCR)
  SamsungPkg/GTS3LLtePkg/Drivers/AcpiTablesDxe/AcpiTablesDxe.inf

  # ------------------------------------------------------------------
  # Security
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf

  # ------------------------------------------------------------------
  # Console / HII
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf {
    <LibraryClasses>
      PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
  }
  MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
  MdeModulePkg/Universal/Console/TerminalDxe/TerminalDxe.inf
  MdeModulePkg/Universal/SerialDxe/SerialDxe.inf
  MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf

  # ------------------------------------------------------------------
  # Block device / disk
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  FatPkg/EnhancedFatDxe/Fat.inf

  # ------------------------------------------------------------------
  # USB Host + Mass Storage
  # ------------------------------------------------------------------
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf

  # ------------------------------------------------------------------
  # SMBIOS
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/SmbiosDxe/SmbiosDxe.inf

  # ------------------------------------------------------------------
  # Network (stub — can be wired up to QCA6174 driver later)
  # ------------------------------------------------------------------
  NetworkPkg/DpcDxe/DpcDxe.inf
  NetworkPkg/MnpDxe/MnpDxe.inf
  NetworkPkg/Ip4Dxe/Ip4Dxe.inf
  NetworkPkg/Dhcp4Dxe/Dhcp4Dxe.inf
  NetworkPkg/Udp4Dxe/Udp4Dxe.inf

  # ------------------------------------------------------------------
  # Boot manager
  # ------------------------------------------------------------------
  MdeModulePkg/Universal/BdsDxe/BdsDxe.inf
  MdeModulePkg/Application/UiApp/UiApp.inf {
    <LibraryClasses>
      NULL|MdeModulePkg/Library/DeviceManagerUiLib/DeviceManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootManagerUiLib/BootManagerUiLib.inf
      NULL|MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerUiLib.inf
  }

  # ------------------------------------------------------------------
  # UEFI Shell
  # ------------------------------------------------------------------
  ShellPkg/Application/Shell/Shell.inf {
    <LibraryClasses>
      NULL|ShellPkg/Library/UefiShellLevel2CommandsLib/UefiShellLevel2CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel1CommandsLib/UefiShellLevel1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellLevel3CommandsLib/UefiShellLevel3CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDriver1CommandsLib/UefiShellDriver1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellAcpiViewCommandLib/UefiShellAcpiViewCommandLib.inf
      NULL|ShellPkg/Library/UefiShellInstall1CommandsLib/UefiShellInstall1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellDebug1CommandsLib/UefiShellDebug1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellNetwork1CommandsLib/UefiShellNetwork1CommandsLib.inf
      NULL|ShellPkg/Library/UefiShellNetwork2CommandsLib/UefiShellNetwork2CommandsLib.inf
      ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
      ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
      ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf
      HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
      BcfgCommandLib|ShellPkg/Library/UefiShellBcfgCommandLib/UefiShellBcfgCommandLib.inf
      FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  }
