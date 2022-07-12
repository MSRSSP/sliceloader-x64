#!/bin/bash -e

source ./run_common.sh

# Default VM parameters
MEM_GB=$DEFAULT_MEM_GB
CPUS=$DEFAULT_CPUS
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
    ;;

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
echo "  Virt Fn ID:    $SRIOV_VF"
echo "  NUMA node:     $NUMA_NODE"

KCMD="console=ttyS0 root=/dev/nvme0n1p1 ro ds=nocloud"

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


# Create SR-IOV virtual function for NIC/NVME
for vfnid in $(setup_sriov_nic $SRIOV_NIC_PF $SRIOV_VF) $(setup_sriov_nvme $SRIOV_NVME_PF $SRIOV_VF); do
  # bind to vfio-pci
  echo vfio-pci > /sys/bus/pci/devices/$vfnid/driver_override
  echo $vfnid >/sys/bus/pci/drivers_probe
  CMD="$CMD -device vfio-pci,host=$vfnid"
done

set -x
numactl -N $NUMA_NODE $CMD -kernel vmlinuz -initrd initrd.img -append "$KCMD"
tput smam
