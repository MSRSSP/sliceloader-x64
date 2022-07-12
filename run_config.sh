# PCIe serial ports to be used as slice consoles
PCI_SERIAL_CONSOLES=( 0000:21:00.0 0000:22:00.0 0000:22:00.1 )

# host PF for SR-IOV NIC
SRIOV_NIC_PF="0000:99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="0000:8d:00.0"

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

# Default guest resources
DEFAULT_MEM_GB=16
DEFAULT_CPUS=8
