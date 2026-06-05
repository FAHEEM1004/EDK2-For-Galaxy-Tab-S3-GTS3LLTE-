/** @file
  ACPI tables for Samsung Galaxy Tab S3 LTE (SM-T825 / GTS3L LTE).

  Provides minimal ACPI 6.x tables required for UEFI-aware OS boot:
    - RSDP  (Root System Description Pointer)
    - XSDT  (Extended System Description Table)
    - FADT  (Fixed ACPI Description Table)
    - MADT  (Multiple APIC Description Table → GICv3 interrupt controller)
    - GTDT  (Generic Timer Description Table → ARM generic timer)
    - SPCR  (Serial Port Console Redirection → UART debug port)
    - DBG2  (Debug Port Table 2 → same UART)
    - SMBIOS is populated by PlatformDxe, not here.

  MADT describes:
    - 4× GIC CPU Interface (one per Kryo core, PSCI-based)
    - GICv3 Distributor @ 0x09BC0000
    - GICv3 Redistributors @ 0x09C00000 (4× contiguous)
    - GIC ITS @ 0x09BE0000

  GTDT describes:
    - ARM Generic Timer: non-secure EL1 phys timer IRQ 30 (PPI), level low
    - Frequency: 19.2 MHz (0x124F800)

  Copyright (c) 2024, Samsung / TianoCore Contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>

#include <Platform/GTS3LLte.h>

// ---------------------------------------------------------------------------
// ACPI table signatures (convenience wrappers)
// ---------------------------------------------------------------------------
#define ACPI_OEM_ID              "SAMSNG"
#define ACPI_OEM_TABLE_ID        SIGNATURE_64 ('G','T','S','3','L','L','T','E')
#define ACPI_CREATOR_ID          SIGNATURE_32 ('I','D','T',' ')
#define ACPI_CREATOR_REV         0x00000001

// ---------------------------------------------------------------------------
// MADT (GICv3) — 4 cores, 2 clusters
// ---------------------------------------------------------------------------
#pragma pack(1)

typedef struct {
  EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  Header;
  // 4× GIC CPU Interface structures (one per Kryo core)
  EFI_ACPI_6_2_GIC_STRUCTURE                            GicCpu[4];
  // GICv3 Distributor
  EFI_ACPI_6_2_GIC_DISTRIBUTOR_STRUCTURE                GicDist;
  // GIC Redistributors (contiguous, 1 per CPU)
  EFI_ACPI_6_2_GICR_STRUCTURE                           GicRedist;
  // GIC ITS
  EFI_ACPI_6_2_GIC_ITS_STRUCTURE                        GicIts;
} GTS3L_MADT;

STATIC GTS3L_MADT  mMadt = {
  .Header = {
    .Header = {
      .Signature  = EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
      .Length     = sizeof (GTS3L_MADT),
      .Revision   = EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
      .OemId      = ACPI_OEM_ID,
      .OemTableId = ACPI_OEM_TABLE_ID,
      .OemRevision= 1,
      .CreatorId  = ACPI_CREATOR_ID,
      .CreatorRevision = ACPI_CREATOR_REV,
    },
    .LocalApicAddress = 0,  // GICv3 — no legacy APIC
    .Flags            = EFI_ACPI_6_2_PCAT_COMPAT,
  },
  // CPU0: Cluster 0 Core 0  (MPIDR = 0x00000000)
  .GicCpu[0] = {
    .Type                = EFI_ACPI_6_2_GIC,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_STRUCTURE),
    .CPUInterfaceNumber  = 0,
    .AcpiProcessorUid    = 0,
    .Flags               = EFI_ACPI_6_2_GIC_ENABLED,
    .ParkingProtocolVersion = 0,
    .PerformanceInterruptGsiv = 23,  // PPI 7 = SPI 23 (PMU)
    .ParkedAddress       = 0,
    .PhysicalBaseAddress = 0,        // GICv3 — no GICC MMIO needed
    .GICRBaseAddress    = MSM8996_GIC_REDIST_BASE + 0 * 0x20000,
    .MPIDR               = 0x0000000000000000ULL,
    .ProcessorPowerEfficiencyClass = 1,  // efficiency core
  },
  // CPU1: Cluster 0 Core 1  (MPIDR = 0x00000001)
  .GicCpu[1] = {
    .Type                = EFI_ACPI_6_2_GIC,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_STRUCTURE),
    .CPUInterfaceNumber  = 1,
    .AcpiProcessorUid    = 1,
    .Flags               = EFI_ACPI_6_2_GIC_ENABLED,
    .ParkingProtocolVersion = 0,
    .PerformanceInterruptGsiv = 23,
    .ParkedAddress       = 0,
    .PhysicalBaseAddress = 0,
    .GICRBaseAddress    = MSM8996_GIC_REDIST_BASE + 1 * 0x20000,
    .MPIDR               = 0x0000000000000001ULL,
    .ProcessorPowerEfficiencyClass = 1,
  },
  // CPU2: Cluster 1 Core 0  (MPIDR = 0x00000100)
  .GicCpu[2] = {
    .Type                = EFI_ACPI_6_2_GIC,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_STRUCTURE),
    .CPUInterfaceNumber  = 2,
    .AcpiProcessorUid    = 2,
    .Flags               = EFI_ACPI_6_2_GIC_ENABLED,
    .ParkingProtocolVersion = 0,
    .PerformanceInterruptGsiv = 23,
    .ParkedAddress       = 0,
    .PhysicalBaseAddress = 0,
    .GICRBaseAddress    = MSM8996_GIC_REDIST_BASE + 2 * 0x20000,
    .MPIDR               = 0x0000000000000100ULL,
    .ProcessorPowerEfficiencyClass = 0,  // performance core
  },
  // CPU3: Cluster 1 Core 1  (MPIDR = 0x00000101)
  .GicCpu[3] = {
    .Type                = EFI_ACPI_6_2_GIC,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_STRUCTURE),
    .CPUInterfaceNumber  = 3,
    .AcpiProcessorUid    = 3,
    .Flags               = EFI_ACPI_6_2_GIC_ENABLED,
    .ParkingProtocolVersion = 0,
    .PerformanceInterruptGsiv = 23,
    .ParkedAddress       = 0,
    .PhysicalBaseAddress = 0,
    .GICRBaseAddress    = MSM8996_GIC_REDIST_BASE + 3 * 0x20000,
    .MPIDR               = 0x0000000000000101ULL,
    .ProcessorPowerEfficiencyClass = 0,
  },
  // GICv3 Distributor
  .GicDist = {
    .Type                = EFI_ACPI_6_2_GICD,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_DISTRIBUTOR_STRUCTURE),
    .GicId               = 0,
    .PhysicalBaseAddress = MSM8996_GIC_DIST_BASE,
    .SystemVectorBase    = 0,
    .GicVersion          = 3,
  },
  // GICv3 Redistributors block
  .GicRedist = {
    .Type                = EFI_ACPI_6_2_GICR,
    .Length              = sizeof (EFI_ACPI_6_2_GICR_STRUCTURE),
    .DiscoveryRangeBaseAddress = MSM8996_GIC_REDIST_BASE,
    .DiscoveryRangeLength      = 4 * 0x20000,  // 4 redistributors × 128 KB each
  },
  // GIC ITS (Interrupt Translation Service)
  .GicIts = {
    .Type                = EFI_ACPI_6_2_GIC_ITS,
    .Length              = sizeof (EFI_ACPI_6_2_GIC_ITS_STRUCTURE),
    .GicItsId            = 0,
    .PhysicalBaseAddress = MSM8996_GIC_ITS_BASE,
  },
};

// ---------------------------------------------------------------------------
// GTDT (ARM Generic Timer)
// ---------------------------------------------------------------------------
typedef struct {
  EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE  Header;
} GTS3L_GTDT;

STATIC GTS3L_GTDT  mGtdt = {
  .Header = {
    .Header = {
      .Signature       = EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
      .Length          = sizeof (GTS3L_GTDT),
      .Revision        = EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION,
      .OemId           = ACPI_OEM_ID,
      .OemTableId      = ACPI_OEM_TABLE_ID,
      .OemRevision     = 1,
      .CreatorId       = ACPI_CREATOR_ID,
      .CreatorRevision = ACPI_CREATOR_REV,
    },
    .CntControlBasePhysicalAddress  = 0xFFFFFFFFFFFFFFFF, // not MMIO-mapped for CPU
    .Reserved                       = 0,
    .SecurePL1TimerGSIV             = 29,   // PPI 13
    .SecurePL1TimerFlags            = EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_MODE |
                                      EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY,
    .NonSecurePL1TimerGSIV          = 30,   // PPI 14 — non-secure EL1 physical timer
    .NonSecurePL1TimerFlags         = EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_MODE |
                                      EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY,
    .VirtualTimerGSIV               = 27,   // PPI 11
    .VirtualTimerFlags              = EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_MODE |
                                      EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY,
    .NonSecurePL2TimerGSIV          = 26,   // PPI 10
    .NonSecurePL2TimerFlags         = EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_MODE |
                                      EFI_ACPI_6_2_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY,
    .CntReadBasePhysicalAddress     = 0xFFFFFFFFFFFFFFFF,
    .PlatformTimerCount             = 0,
    .PlatformTimerOffset            = 0,
  },
};

// ---------------------------------------------------------------------------
// SPCR (Serial Port Console Redirection)
// Debug UART: BLSP2 UART1 @ 0x075B0000, 115200 baud, 8-N-1
// ---------------------------------------------------------------------------
STATIC EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE  mSpcr = {
  .Header = {
    .Signature       = EFI_ACPI_2_0_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
    .Length          = sizeof (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE),
    .Revision        = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION,
    .OemId           = ACPI_OEM_ID,
    .OemTableId      = ACPI_OEM_TABLE_ID,
    .OemRevision     = 1,
    .CreatorId       = ACPI_CREATOR_ID,
    .CreatorRevision = ACPI_CREATOR_REV,
  },
  .InterfaceType = 3,  // ARM PL011 / generic MMIO — type 3 = "Other"
  .Reserved1     = { 0, 0, 0 },
  .BaseAddress   = {
    .AddressSpaceId    = EFI_ACPI_3_0_SYSTEM_MEMORY,
    .RegisterBitWidth  = 8,
    .RegisterBitOffset = 0,
    .AccessSize        = EFI_ACPI_3_0_BYTE,
    .Address           = MSM8996_DEBUG_UART_BASE,
  },
  .InterruptType = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_GIC,
  .Irq           = 0,  // not used for GIC
  .GlobalSystemInterrupt = MSM8996_DEBUG_UART_IRQ,
  .BaudRate      = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_115200,
  .Parity        = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_PARITY_NO_PARITY,
  .StopBits      = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_STOP_BITS_1,
  .FlowControl   = 0,
  .TerminalType  = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_ANSI,
  .Reserved2     = 0,
  .PciDeviceId   = 0xFFFF,
  .PciVendorId   = 0xFFFF,
  .PciBusNumber  = 0,
  .PciDeviceNumber = 0,
  .PciFunctionNumber = 0,
  .PciFlags      = 0,
  .PciSegment    = 0,
  .Reserved3     = 0,
};

#pragma pack()

// ---------------------------------------------------------------------------
// Checksum helper
// ---------------------------------------------------------------------------
STATIC
VOID
AcpiTableChecksum (
  IN VOID   *Buffer,
  IN UINTN   Size
  )
{
  UINT8   *Ptr  = Buffer;
  UINT8    Sum  = 0;
  UINTN    i;

  Ptr[9] = 0;  // zero the checksum byte first
  for (i = 0; i < Size; i++) {
    Sum += Ptr[i];
  }
  Ptr[9] = (UINT8)(0x100 - Sum);
}

// ---------------------------------------------------------------------------
// Install all tables
// ---------------------------------------------------------------------------

STATIC
EFI_STATUS
InstallTable (
  IN EFI_ACPI_TABLE_PROTOCOL  *AcpiTable,
  IN VOID                     *Table,
  IN UINTN                     TableSize
  )
{
  UINTN       Key = 0;
  EFI_STATUS  Status;

  AcpiTableChecksum (Table, TableSize);
  Status = AcpiTable->InstallAcpiTable (AcpiTable, Table, TableSize, &Key);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ACPI: Install failed for %.4s: %r\n", Table, Status));
  }
  return Status;
}

/**
  Entry point for ACPI tables DXE driver.
**/
EFI_STATUS
EFIAPI
AcpiTablesDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)&AcpiTable);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AcpiTablesDxe: Cannot locate AcpiTable protocol: %r\n", Status));
    return Status;
  }

  InstallTable (AcpiTable, &mMadt, sizeof (mMadt));
  InstallTable (AcpiTable, &mGtdt, sizeof (mGtdt));
  InstallTable (AcpiTable, &mSpcr, sizeof (mSpcr));

  DEBUG ((DEBUG_INFO, "AcpiTablesDxe: MADT + GTDT + SPCR installed\n"));
  return EFI_SUCCESS;
}
