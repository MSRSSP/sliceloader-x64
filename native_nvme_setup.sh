#!/bin/bash -ex

# This script configures virtual functions in the host, to try to mimic their performance in guests.

# PCI segment number (of everything)
PCI_SEG=0000

# host PF for SR-IOV NIC
SRIOV_NIC_PF="99:00.0"

# host PF for SR-IOV NVME
SRIOV_NVME_PF="8d:00.0"

# VF instance to use
SRIOV_VF=0

# base MAC address for the NIC VFs
NIC_VF_MACADDR_BASE="02:22:33:44:55:66"

#
# Setup the NIC
#
sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$SRIOV_NIC_PF

for name in $sysfsdir/net/*; do
  if [ -d "$name" ]; then
    ifname=$(basename "$name")
    break
  fi
done

echo 1 > $sysfsdir/sriov_numvfs

ip link set $ifname vf $SRIOV_VF mac $NIC_VF_MACADDR_BASE

ifconfig enp153s0f0v0 up 192.168.37.9 netmask 255.255.255.0
ifconfig enp153s0f0np0 down

#
# Setup NVME
#

service docker stop
if mount | grep -q /var/lib/docker; then umount /var/lib/docker; fi

sysfsdir=/sys/bus/pci/devices/$PCI_SEG:$SRIOV_NVME_PF
for name in $sysfsdir/nvme/*; do
  if [ -d "$name" ]; then
    nvme_dev=/dev/$(basename "$name")
    break
  fi
done

# XXX: assign *2* VQ & VI resources, to match guest eval for slices/VMs
nres=2 # 9
nvme virt-mgmt $nvme_dev -c 1 -r 0 -n $nres -a 8
nvme virt-mgmt $nvme_dev -c 1 -r 1 -n $nres -a 8

# create *all* the VFs, but enable only the first
cat $sysfsdir/sriov_totalvfs > $sysfsdir/sriov_numvfs

nvme virt-mgmt $nvme_dev -c 1 -a 9

echo nvme > /sys/bus/pci/devices/0000:8d:00.1/driver_override

echo 0000:8d:00.0 > /sys/bus/pci/drivers/nvme/unbind

sleep 10

echo 0000:8d:00.1 > /sys/bus/pci/drivers_probe

sleep 10

mount /dev/nvme0n2p1 /var/lib/docker

service docker start
