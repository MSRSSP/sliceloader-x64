#!/bin/bash -e

source ./run_common.sh

# Default parameters
MEM_GB=$DEFAULT_MEM_GB
CPUS=$DEFAULT_CPUS

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN="$PCI_SERIAL_CONSOLE"

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

  -v)
    SRIOV_VF="$2"
    shift
    ;;
  
  -h)
    echo "Usage: $0 [args]"
    echo "   -m GIB       set memory size in GiB"
    echo "   -c CPUS      set number of VCPUs"
    echo "   -v VFID      set virtual function ID to use"
    exit 0
    ;;

  *)
    echo "Error: unknown argument: $1"
    exit 1
    ;;
  esac

  shift
done

echo "Slice configuration:"
echo "  CPUs:          $CPUS"
echo "  RAM:           $MEM_GB GiB"
echo "  Virt Fn ID:    $SRIOV_VF"

# find the IO port occupied by the serial console
sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$PCI_SERIAL_CONSOLE
read class < $sysfsdir/class
if [ "$class" != "0x070002" ]; then
  echo "Error: $PCI_SERIAL_CONSOLE is not a 16550 serial port"
  exit 1
fi
read SERIAL_IOPORT_BASE SERIAL_IOPORT_LIMIT flags < $sysfsdir/resource
if (( (flags >> 8 & 0xff) != 1 )); then
  echo "Error: $PCI_SERIAL_CONSOLE doesn't implement an I/O port resource"
  exit 1
fi

# Create SR-IOV virtual function for NIC/NVME
for pf in $SRIOV_NIC_PF $SRIOV_NVME_PF; do
  setup_sriov $pf
done

# Ensure that all assigned devices exist and are not bound to drivers on the host
for dev in $PCI_ASSIGN; do
  sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$dev
  if [ ! -d $sysfsdir ]; then
    echo "Error: device $dev does not exist"
    exit 1
  fi
  if [ -e $sysfsdir/driver ]; then
    echo "Unbinding PCI device $dev from host driver $(basename $(readlink $sysfsdir/driver))"
    echo -n $PCI_SEG:$dev > $sysfsdir/driver/unbind
  fi
done

probe_only_arg=""
for dev in $PCI_ASSIGN; do
  if [ -n "$probe_only_arg" ]; then
    probe_only_arg="$probe_only_arg;$dev"
  else
    probe_only_arg="$dev"
  fi
done

# enable plenty of debug output, and configure the console
CMDLINE=""
#CMDLINE="$CMDLINE loglevel=7 apic=debug"
CMDLINE="$CMDLINE console=uart,io,$SERIAL_IOPORT_BASE,115200n8"
CMDLINE="$CMDLINE root=/dev/nvme0n1p1 ro"

# disable use of the IO-APICs
CMDLINE="$CMDLINE noapic"

# PCI config:
# nobios: disable search for legacy PCI BIOS (if not enabled in the kernel config, this prints an unknown option warning)
# norom,nobar: don't assign bars
# realloc=off: don't reallocate PCI bridge resources
# lastbus=0x100: this out-of-bounds valud disables the legacy bus scan in pcibios_fixup_peer_bridges(),
#                preventing discovery of any PCI buses not exposed via ACPI
# permit_probe_only: allow-list of configured devices to probe (relies on custom kernel)
CMDLINE="$CMDLINE pci=nobios,norom,nobar,realloc=off,lastbus=0x100,permit_probe_only=$probe_only_arg"

# instruct our pci-vf-as-pf driver where to find our SR-IOV devices, and which VF to use
CMDLINE="$CMDLINE pci-vf-as-pf.pf=$SRIOV_NIC_PF,$SRIOV_NVME_PF pci-vf-as-pf.vf=$SRIOV_VF"

# Physical base of memory on target NUMA node
# This is derived from ACPI SRAT (sudo acpidump -n SRAT -b && iasl -d srat.dat)
NUMA1_PHYS_BASE=0x880000000

# First core ID on target NUMA node
NUMA1_CORE_BASE=12

# Base address of RAM for this slice
# XXX: this assumes that all slices will have the same memory size, and live on the same NUMA node
RAM_PHYS_BASE=$((NUMA1_PHYS_BASE + SRIOV_VF * MEM_GB * 0x40000000))

# Starting core forthis slice
# XXX: same assumption as above
CORE_BASE=$((NUMA1_CORE_BASE + SRIOV_VF * CPUS))

sync # lingering paranoia

set -x

# -initrd rootfs.cpio.gz
builddir/runslice -rambase $RAM_PHYS_BASE \
  -ramsize $((MEM_GB * 0x40000000)) \
  -cpus $CORE_BASE-$((CORE_BASE + CPUS - 1)) \
  -kernel bzImage -dsdt builddir/dsdt.aml \
  -cmdline "$CMDLINE"
