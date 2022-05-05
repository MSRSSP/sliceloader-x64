DefinitionBlock (
  "",
  "DSDT",
  0x02,         // Table revision
  "SLICER",     // OEM ID
  "SLICE   ",   // OEM table ID
  0x0           // OEM revision
)
{
  Scope(\_SB)
  {
    // Processors
    Device(PR00) {          // Unique name for Processor 0.
      Name(_HID,"ACPI0007")
      Name(_UID,0)          // Unique ID for Processor 0.
    }
    Device(PR01) {
      Name(_HID,"ACPI0007")
      Name(_UID,1)
    }

    // PCI host
    Device(PCI0)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_ADR, 0x00000000)
      Name(_BBN, 0)
      Name(_UID, 0)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0000,             // Range Minimum
            0x00FF,             // Range Maximum
            0x0000,             // Translation Offset
            0x0100)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x00000000,         // Range Minimum
            0x00000CF7,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00000CF8,         // Length
            ,, , TypeStatic, DenseTranslation)
        IO (Decode16,
            0x0CF8,             // Range Minimum
            0x0CF8,             // Range Maximum
            0x01,               // Alignment
            0x08,               // Length
            )
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x00000D00,         // Range Minimum
            0x0000FFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0000F300,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000A0000,         // Range Minimum
            0x000BFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00020000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xbe800000,         // Range Minimum
            0xdfffffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x21800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xFD000000,         // Range Minimum
            0xFE7FFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x01800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Prefetchable, ReadWrite,
            0x0,
            0x1c00000000,
            0x1fffffffff,
            0x0,
            0x400000000
            ,,,)
      })
    }
  }
}
