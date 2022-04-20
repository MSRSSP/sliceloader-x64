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
      Name(_UID, 99)
      Name(_STA, 0xf)
      Name(_CRS, ResourceTemplate() {
        IO(Decode16,0x3000,0x3000,1,8)
      })
    }
  }
}
