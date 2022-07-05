# PCI segment number (of everything)
PCI_SEG=0000

# PCIe serial port to be used as console
PCI_SERIAL_CONSOLE=21:00.0

# host PF for SR-IOV NIC
SRIOV_NIC_PF="99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="8d:00.0"

# Default VF instance to use
SRIOV_VF=0

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

# Default guest resources
DEFAULT_MEM_GB=16
DEFAULT_CPUS=8
