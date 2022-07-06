# PCI segment number (of everything)
PCI_SEG=0000

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN=""

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
function setup_sriov {
  local sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$1
  if [ ! -e $sysfsdir/sriov_numvfs ]; then
    echo "Error: $1 does not exist or does not support SR-IOV"
    exit 1
  fi

  local numvfs
  read numvfs < $sysfsdir/sriov_numvfs

  local ifname=""
  local nvme_dev=""
  if [ -d $sysfsdir/net ]; then
    # find the network interface name on the host
    local name
    for name in $sysfsdir/net/*; do
      if [ -d "$name" ]; then
        ifname=$(basename "$name")
        break
      fi
    done
  elif [ -d $sysfsdir/nvme ]; then
    local name devname
    for name in $sysfsdir/nvme/*; do
      if [ -d "$name" ]; then
        devname=$(basename "$name")
        break
      fi
    done
    nvme_dev=/dev/$devname
    local nvme_scid=$((SRIOV_VF + 1)) # TODO: get this from nvme list-secondary

    if [ $numvfs -eq 0 ]; then
      for i in $(seq 1 4); do
        # assign 2 queues per VF
        nvme virt-mgmt $nvme_dev -c $i -r 0 -n 2 -a 8
        nvme virt-mgmt $nvme_dev -c $i -r 1 -n 2 -a 8
      done
    fi
  fi

  if [ $numvfs -eq 0 ]; then
    # prevent probing of virtual function drivers, then create all the VFs
    echo -n 0 > $sysfsdir/sriov_drivers_autoprobe
    cat $sysfsdir/sriov_totalvfs > $sysfsdir/sriov_numvfs
  fi

  if [ -n "$ifname" ]; then
    local mac=$(($(echo 0x$NIC_VF_MACADDR_BASE | tr -d ':') + SRIOV_VF))
    mac=$(printf "%012x" $mac | sed 's/../&:/g;s/:$//')
    echo "Assigning MAC $mac to $ifname VF $SRIOV_VF"
    ip link set $ifname vf $SRIOV_VF mac $mac
  else
    nvme virt-mgmt $nvme_dev -c $nvme_scid -a 9
  fi

  # add the virtual function's PCI ID to the list of assigned devices
  vfnid=$(basename $(readlink $sysfsdir/virtfn$SRIOV_VF))
  echo "Assigning SR-IOV VF $vfnid"
  PCI_ASSIGN="$PCI_ASSIGN  ${vfnid#$PCI_SEG:}"
}