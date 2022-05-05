#!/bin/bash -e

# PCI segment number (of everything)
PCI_SEG=0000

# PCIe serial port to be used as console
PCI_SERIAL_CONSOLE=04:00.0

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN="$PCI_SERIAL_CONSOLE"

# host PF for SR-IOV -- we'll construct and use a single VF
SRIOV_HOST="05:00.1"

# MAC address for the VF
VF_MACADDR="02:22:33:44:55:66"

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

# Create SR-IOV virtual function
sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$SRIOV_HOST
if [ ! -e $sysfsdir/sriov_numvfs ]; then
  echo "Error: $SRIOV_HOST does not exist or does not support SR-IOV"
  exit 1
fi

read numvfs < $sysfsdir/sriov_numvfs
if [ $numvfs -ne 0 ]; then
  echo "Error: virtual functions of $SRIOV_HOST already created"
  exit 1
fi

# find the network interface name on the host
ifname=""
for name in $sysfsdir/net/*; do
  if [ -d "$name" ]; then
    ifname=$(basename "$name")
    break
  fi
done

# prevent probing of virtual function drivers, then create a single VF
echo -n 0 > $sysfsdir/sriov_drivers_autoprobe
echo -n 1 > $sysfsdir/sriov_numvfs

if [ -n "$ifname" ]; then
  echo "Assigning MAC $VF_MACADDR to $ifname VF 0"
  ip link set $ifname vf 0 mac $VF_MACADDR
fi

# add the virtual function's PCI ID to the list of assigned devices
vfnid=$(basename $(readlink $sysfsdir/virtfn0))
echo "Created SR-IOV VF $vfnid"
PCI_ASSIGN="$PCI_ASSIGN ${vfnid#$PCI_SEG:}"

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

CMDLINE="loglevel=7 noapic apic=debug"
CMDLINE="$CMDLINE console=uart,io,$SERIAL_IOPORT_BASE,115200n8"
CMDLINE="$CMDLINE pci=nobios,norom,nobar,realloc=off,permit_probe_only=$probe_only_arg"
CMDLINE="$CMDLINE pci-vf-as-pf.pf=$SRIOV_HOST pci-vf-as-pf.vf=0"

sync; sync; sleep 0.1

set -x

builddir/runslice -rambase 0x180000000 -ramsize 0x40000000 -cpus 1-2 \
  -kernel bzImage -initrd rootfs.cpio.gz -dsdt builddir/dsdt.aml \
  -cmdline "$CMDLINE"
