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
    Device(PR00) {          // Unique name for Processor 0.
      Name(_HID,"ACPI0007")
      Name(_UID,0)          // Unique ID for Processor 0.
    }
  }
}
