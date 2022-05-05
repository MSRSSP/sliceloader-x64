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

    // pci_bus 0000:20: root bus resource [bus 20-2b]
    // pci_bus 0000:20: root bus resource [io  0x6000-0x7fff window]
    // pci_bus 0000:20: root bus resource [mem 0xab000000-0xb87fffff window]
    // pci_bus 0000:20: root bus resource [mem 0x388000000000-0x38bfffffffff window]
    Device(PCI0)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_BBN, 0x20)
      Name(_UID, 0)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0020,             // Range Minimum
            0x002B,             // Range Maximum
            0x0000,             // Translation Offset
            0x000C)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x00006000,         // Range Minimum
            0x00007FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00002000,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xab000000,         // Range Minimum
            0xb87fffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0d800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Prefetchable, ReadWrite,
            0x0,
            0x388000000000,
            0x38bfffffffff,
            0x0,
            0x4000000000
            ,,,)
      })
    }

    // pci_bus 0000:98: root bus resource [bus 98-a3]
    // pci_bus 0000:98: root bus resource [io  0xc000-0xdfff window]
    // pci_bus 0000:98: root bus resource [mem 0xe1000000-0xee7fffff window]
    // pci_bus 0000:98: root bus resource [mem 0x398000000000-0x39bfffffffff window]
    Device(PCI1)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_BBN, 0x98)
      Name(_UID, 1)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0098,             // Range Minimum
            0x00A3,             // Range Maximum
            0x0000,             // Translation Offset
            0x000c)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x0000C000,         // Range Minimum
            0x0000DFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00002000,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xe1000000,         // Range Minimum
            0xee7fffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0d800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Prefetchable, ReadWrite,
            0x0,
            0x398000000000,
            0x39bfffffffff,
            0x0,
            0x4000000000
            ,,,)
      })
    }
  }
}
