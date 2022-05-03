#!/bin/bash -e

# PCI segment number (of everything)
PCI_SEG=0000

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN="04:00.0"

# host PFs for SR-IOV
#SRIOV_HOSTS="01:00.0 05:00.0"
SRIOV_HOSTS="05:00.1"
VF_MACADDR="02:22:33:44:55:66"

# Create SR-IOV virtual functions
for dev in $SRIOV_HOSTS; do
  sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$dev
  if [ ! -e $sysfsdir/sriov_numvfs ]; then
    echo "Error: $dev does not exist or does not support SR-IOV"
    exit 1
  fi

  read numvfs < $sysfsdir/sriov_numvfs
  if [ $numvfs -ne 0 ]; then
    echo "Error: virtual functions of $dev already created"
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

CMDLINE="loglevel=7 noapic apic=debug"
CMDLINE="$CMDLINE console=uart,io,0x3000,115200n8"
CMDLINE="$CMDLINE pci=nobios,norom,nobar,realloc=off,permit_probe_only=$probe_only_arg"

sync; sync; sleep 0.1

set -x

builddir/runslice -rambase 0x180000000 -ramsize 0x40000000 -cpus 4,6 \
  -kernel bzImage -initrd rootfs.cpio.gz -dsdt builddir/dsdt.aml \
  -cmdline "$CMDLINE"
