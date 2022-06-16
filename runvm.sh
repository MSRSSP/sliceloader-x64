#!/bin/bash -e

# PCI segment number (of everything)
PCI_SEG=0000

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN=""

# host PF for SR-IOV NIC
SRIOV_NIC_PF="99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="8d:00.0"

# VF instance to use
SRIOV_VF=0

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

# Default VM parameters
MEM_GB=24
CPUS=8
HUGEPAGE_MB=0
NUMA_NODE=1

# Parse arguments
while [[ $# -gt 0 ]]
do
  case "$1" in
  -m)
    MEM_GB="$2"
    shift
    ;;

  -c)
    CPUS="$2"
    shift
    ;;

  -p)
    HUGEPAGE_MB="$2"
    shift
    ;;

  -v)
    SRIOV_VF="$2"
    shift
    ;;
  
  -n)
    NUMA_NODE="$2"
    shift
    ;;

  -h)
    echo "Usage: $0 [args]"
    echo "   -m GIB       set memory size in GiB"
    echo "   -c CPUS      set number of VCPUs"
    echo "   -p 0|2|1024  set EPT page size in MiB"
    echo "   -v VFID      set virtual function ID to use"
    echo "   -n NODEID    set NUMA node for memory and host CPUs"
    exit 0

  *)
    echo "Error: unknown argument: $1"
    exit 1
    ;;
  esac

  shift
done

echo "VM configuration:"
echo "  CPUs:          $CPUS"
echo "  RAM:           $MEM_GB GiB"
echo "  EPT page size: $HUGEPAGE_MB MiB"
echo "  NUMA node:     $NUMA_NODE"

# Create SR-IOV virtual function for NIC
for pf in $SRIOV_NIC_PF $SRIOV_NVME_PF; do
  sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$pf
  if [ ! -e $sysfsdir/sriov_numvfs ]; then
    echo "Error: $pf does not exist or does not support SR-IOV"
    exit 1
  fi

  read numvfs < $sysfsdir/sriov_numvfs
  if [ $numvfs -eq 0 ]; then
    ifname=""
    nvme_dev=""
    if [ -d $sysfsdir/net ]; then
      # find the network interface name on the host
      for name in $sysfsdir/net/*; do
        if [ -d "$name" ]; then
          ifname=$(basename "$name")
          break
        fi
      done
    elif [ -d $sysfsdir/nvme ]; then
      for name in $sysfsdir/nvme/*; do
        if [ -d "$name" ]; then
          devname=$(basename "$name")
          break
        fi
      done
      nvme_dev=/dev/$devname
      nvme_scid=$((SRIOV_VF + 1))

      # assign 2 queues per VF
      nvme virt-mgmt $nvme_dev -c $nvme_scid -r 0 -n 2 -a 8
      nvme virt-mgmt $nvme_dev -c $nvme_scid -r 1 -n 2 -a 8
    fi

    # prevent probing of virtual function drivers, then create all the VFs
    echo -n 0 > $sysfsdir/sriov_drivers_autoprobe
    cat $sysfsdir/sriov_totalvfs > $sysfsdir/sriov_numvfs

    if [ -n "$ifname" ]; then
      mac=$(($(echo 0x$NIC_VF_MACADDR_BASE | tr -d ':') + SRIOV_VF))
      mac=$(printf "%012x" $mac | sed 's/../&:/g;s/:$//')
      echo "Assigning MAC $mac to $ifname VF $SRIOV_VF"
      ip link set $ifname vf $SRIOV_VF mac $mac
    else
      nvme virt-mgmt $nvme_dev -c $nvme_scid -a 9
    fi
  fi

  # add the virtual function's PCI ID to the list of assigned devices
  vfnid=$(basename $(readlink $sysfsdir/virtfn$SRIOV_VF))
  echo "Assigning SR-IOV VF $vfnid"
  PCI_ASSIGN="$PCI_ASSIGN $vfnid"

  # bind it to vfio-pci
  sysfsdir=/sys/bus/pci/devices/$vfnid
  echo vfio-pci > $sysfsdir/driver_override
  echo $vfnid >/sys/bus/pci/drivers_probe
done

KCMD="console=ttyS0 root=/dev/nvme0n1p1 ro"

CMD="qemu-system-x86_64 \
 -machine q35,usb=off \
 -accel kvm -cpu host -smp $CPUS"

case $HUGEPAGE_MB in
  1024)
    HUGEPAGE_PATH=/dev/hugepages1G
    if ! grep -q "^none $HUGEPAGE_PATH " /proc/mounts; then
      mkdir -p $HUGEPAGE_PATH
      mount -t hugetlbfs -o pagesize=1G none $HUGEPAGE_PATH
    fi
    ;;

  2)
    HUGEPAGE_PATH=/dev/hugepages
    ;;

  0)
    HUGEPAGE_PATH=""
    ;;

  *)
    echo "Error: invalid page size ($HUGEPAGE_MB)"
    exit 1
    ;;
esac

# https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html#interaction-of-task-memory-policy-with-huge-page-allocation-freeing
for f in /sys/kernel/mm/hugepages/hugepages-*/nr_hugepages; do
  echo 0 > $f
done

CMD="$CMD -m $((MEM_GB * 1024)) -mem-prealloc -overcommit mem-lock=on"

if [ -n "$HUGEPAGE_PATH" ]; then
  echo "(Re)allocating huge pages on node $NUMA_NODE"
  numactl -m $NUMA_NODE echo $((MEM_GB * 1024 / HUGEPAGE_MB)) > /sys/kernel/mm/hugepages/hugepages-$((HUGEPAGE_MB * 1024))kB/nr_hugepages_mempolicy
  CMD="$CMD -mem-path $HUGEPAGE_PATH"
fi

CMD="$CMD -display none -nodefaults \
 -chardev stdio,id=char0,mux=on \
 -device isa-serial,chardev=char0,id=serial0 \
 -mon chardev=char0,mode=readline"

for dev in $PCI_ASSIGN; do
  CMD="$CMD -device vfio-pci,host=$dev"
done

set -x
numactl -N $NUMA_NODE $CMD -kernel bzImage -append "$KCMD"
tput smam
