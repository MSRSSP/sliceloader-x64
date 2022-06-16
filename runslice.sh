#!/bin/bash -e

# PCI segment number (of everything)
PCI_SEG=0000

# PCIe serial port to be used as console
PCI_SERIAL_CONSOLE=21:00.0

# Non-SR-IOV PCI devices/functions to assign
PCI_ASSIGN="$PCI_SERIAL_CONSOLE"

# host PF for SR-IOV NIC
SRIOV_NIC_PF="99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="8d:00.0"

# VF instance to use
SRIOV_VF=0

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

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

# enable plenty of debug output, and configure the console
CMDLINE="loglevel=7 apic=debug"
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

sync # lingering paranoia

set -x

# -initrd rootfs.cpio.gz
builddir/runslice -rambase 0x880000000 -ramsize 0x600000000 -cpus 12-19 \
  -kernel bzImage -dsdt builddir/dsdt.aml \
  -cmdline "$CMDLINE"
