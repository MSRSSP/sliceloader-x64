DefinitionBlock (
  "dsdt.aml",
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

    // Serial console
    Device(SER0)
    {
      Name(_HID, EisaId("PNP0501")) // 16550A-compatible
      Name(_STA, 0xf)
      Name(_CRS, ResourceTemplate() {
        IO(Decode16,0x3000,0x3000,1,8)
      })
    }

    // PCI host
    Device(PCI0)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_ADR, 0x00000000)
      Name(_BBN, 0)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0000,             // Range Minimum
            0x00FF,             // Range Maximum
            0x0000,             // Translation Offset
            0x0100)             // Length
        DWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
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
        DWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
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
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000C0000,         // Range Minimum
            0x000C3FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000C4000,         // Range Minimum
            0x000C7FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000C8000,         // Range Minimum
            0x000CBFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000CC000,         // Range Minimum
            0x000CFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000D0000,         // Range Minimum
            0x000D3FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000D4000,         // Range Minimum
            0x000D7FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, _Y06, AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000D8000,         // Range Minimum
            0x000DBFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000DC000,         // Range Minimum
            0x000DFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000E0000,         // Range Minimum
            0x000E3FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000E4000,         // Range Minimum
            0x000E7FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000E8000,         // Range Minimum
            0x000EBFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000EC000,         // Range Minimum
            0x000EFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00004000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, Cacheable, ReadWrite,
            0x00000000,         // Granularity
            0x000F0000,         // Range Minimum
            0x000FFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00010000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0x00000000,         // Range Minimum
            0xDFFFFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0xE0000000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x0000000000000000, // Granularity
            0x0000000000010000, // Range Minimum
            0x000000000001FFFF, // Range Maximum
            0x0000000000000000, // Translation Offset
            0x0000000000010000, // Length
            ,, , AddressRangeMemory, TypeStatic)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xFD000000,         // Range Minimum
            0xFE7FFFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x01800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)

        Memory32Fixed(ReadWrite, 0xbe800000, 0x21800000)
        ExtendedMemory(ResourceProducer,PosDecode,MinFixed,MaxFixed,Prefetchable,
          ReadWrite,0x0,0x1c00000000,0x1fffffffff,0x0,0x400000000,,,)
      })
    }

    // PCIe port
    // 00:01.0 PCI bridge: Intel Corporation Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor PCIe Controller (x16) (rev 05) (prog-if 00 [Normal decode])
    // Bus: primary=00, secondary=01, subordinate=01, sec-latency=0
    // I/O behind bridge: [disabled]
    // Memory behind bridge: be800000-be8fffff [size=1M]
    // Prefetchable memory behind bridge: 0000001c06000000-0000001c068fffff [size=9M]
    // Device(PCI0)
    // {
    //   Name(_HID, EisaId("PNP0A08"))
    //   Name(_SEG, 0)
    //   Name(_ADR, 0x00000000)
    //   Name(_BBN, 1)
    //   Name(_CRS, ResourceTemplate() {
    //     ExtendedMemory(ResourceProducer,PosDecode,MinFixed,MaxFixed,NonCacheable,
    //       ReadWrite,0x0,0xbe800000,0xbe8fffff,0x0,0x100000,,,)
    //     ExtendedMemory(ResourceProducer,PosDecode,MinFixed,MaxFixed,Prefetchable,
    //       ReadWrite,0x0,0x1c06000000,0x1c068fffff,0x0,0x900000,,,)
    //   })
    // }

    // Device(ETH0)
    // {
    //   Name(_HID, "ACPI0004") // Module device
    //   Name(_CID, Package(){"PCI\\VEN_8086&DEV_1528"})
    //   Name(_ADR, 0x0000ffff)
    //   Name(_CRS, ResourceTemplate() {
    //     ExtendedMemory(ResourceProducer,PosDecode,MinFixed,MaxFixed,NonCacheable,
    //       ReadWrite,0x0,0xbe800000,0xbe8fffff,0x0,0x100000,,,)
    //     ExtendedMemory(ResourceProducer,PosDecode,MinFixed,MaxFixed,Prefetchable,
    //       ReadWrite,0x0,0x1c06000000,0x1c068fffff,0x0,0x900000,,,)
    //   })
    // }

    // Reserved resources
    Device (RES)
    {
      Name (_HID, EisaId ("PNP0C02"))
      Name (_CRS, ResourceTemplate() {
        Memory32Fixed(ReadWrite,0xe0000000,0x10000000,) // PCIe MCFG region
      })
    }
  }
}
