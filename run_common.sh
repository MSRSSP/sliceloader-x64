# host PF for SR-IOV NIC
SRIOV_NIC_PF="0000:99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="0000:8d:00.0"

# Default VF instance to use
SRIOV_VF=0

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

# Default guest resources
DEFAULT_MEM_GB=16
DEFAULT_CPUS=8

function setup_sriov_nic {
  if [ $# != 2 ]; then
    echo "Usage: $0 PF_PCI_ADDR VF_ID" > /dev/stderr
    return 1
  fi

  local sysfsdir=/sys/bus/pci/devices/$1
  if [ ! -e $sysfsdir/sriov_numvfs ]; then
    echo "Error: $1 does not exist or does not support SR-IOV" > /dev/stderr
    return 1
  fi

  if [ ! -d $sysfsdir/net ]; then
    echo "Error: $1 is not a network interface" > /dev/stderr
    return 1
  fi

  # find the network interface name on the host
  local ifname=""
  local name
  for name in $sysfsdir/net/*; do
    if [ -d "$name" ]; then
      ifname=$(basename "$name")
      break
    fi
  done

  if [ -z "$ifname" ]; then
    echo "Error: failed to find network interface for $1" > /dev/stderr
    return 1
  fi

  local numvfs
  read numvfs < $sysfsdir/sriov_numvfs
  if [ $numvfs -eq 0 ]; then
    # prevent probing of virtual function drivers, then create all the VFs
    echo -n 0 > $sysfsdir/sriov_drivers_autoprobe
    cat $sysfsdir/sriov_totalvfs > $sysfsdir/sriov_numvfs
  fi

  local mac=$(($(echo 0x$NIC_VF_MACADDR_BASE | tr -d ':') + $2))
  mac=$(printf "%012x" $mac | sed 's/../&:/g;s/:$//')
  #echo "Assigning MAC $mac to $ifname VF $2"
  ip link set $ifname vf $2 mac $mac

  # print the PCI ID of the VF
  echo $(basename $(readlink $sysfsdir/virtfn$2))
}

function setup_sriov_nvme {
  if [ $# != 2 ]; then
    echo "Usage: $0 PF_PCI_ADDR VF_ID" > /dev/stderr
    return 1
  fi

  local sysfsdir=/sys/bus/pci/devices/$1
  if [ ! -e $sysfsdir/sriov_numvfs ]; then
    echo "Error: $1 does not exist or does not support SR-IOV" > /dev/stderr
    return 1
  elif [ ! -d $sysfsdir/nvme ]; then
    echo "Error: $1 is not an NVMe controller" > /dev/stderr
    return 1
  fi

  local nvme_dev=""
  local name
  for name in $sysfsdir/nvme/*; do
    if [ -d "$name" ]; then
      nvme_dev=/dev/$(basename "$name")
      break
    fi
  done

  if [ -z "$nvme_dev" -o ! -c "$nvme_dev" ]; then
    echo "Error: failed to find NVMe host device node" > /dev/stderr
    return 1
  fi

  local numvfs
  read numvfs < $sysfsdir/sriov_numvfs
  if [ $numvfs -eq 0 ]; then
    # XXX: assign VQ & VI resources for all the controllers we might need, before enabling any
    # (if we assign these resources later, then the command to online the secondary fails)
    local vq_max=$(nvme primary-ctrl-caps $nvme_dev -o json | jq .vqfrsm)
    local vi_max=$(nvme primary-ctrl-caps $nvme_dev -o json | jq .vifrsm)
    local cid
    for cid in $(nvme list-secondary $nvme_dev -o json | jq '."secondary-controllers"[]|select(."virtual-function-number" <= 4)."secondary-controller-identifier"'); do
      nvme virt-mgmt $nvme_dev -c $cid -r 0 -n $vq_max -a 8 > /dev/null
      nvme virt-mgmt $nvme_dev -c $cid -r 1 -n $vi_max -a 8 > /dev/null
    done

    # prevent probing of virtual function drivers, then create all the VFs
    echo -n 0 > $sysfsdir/sriov_drivers_autoprobe
    cat $sysfsdir/sriov_totalvfs > $sysfsdir/sriov_numvfs
  fi

  # Bring the secondary controller online
  local cid=$(nvme list-secondary $nvme_dev -o json | jq '."secondary-controllers"[]|select(."virtual-function-number"=='$(($2 + 1))')."secondary-controller-identifier"')
  nvme virt-mgmt $nvme_dev -c $cid -a 9 > /dev/null

  # print the PCI ID of the VF
  echo $(basename $(readlink $sysfsdir/virtfn$2))
}
