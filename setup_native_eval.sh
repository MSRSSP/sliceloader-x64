#!/bin/bash -ex

# This script configures virtual functions in the host, to try to mimic their performance in guests.
source ./run_common.sh
source ./run_config.sh

#
# Setup the NIC
#
sysfsdir=/sys/bus/pci/devices/$SRIOV_NIC_PF
for name in $sysfsdir/net/*; do
  if [ -d "$name" ]; then
    ifname_phys=$(basename "$name")
    break
  fi
done

echo 1 > $sysfsdir/sriov_numvfs

for name in $sysfsdir/virtfn0/net/*; do
  if [ -d "$name" ]; then
    ifname_virt=$(basename "$name")
    break
  fi
done

ip link set $ifname_phys vf 0 mac $NIC_VF_MACADDR_BASE
ifconfig $ifname_virt up 192.168.37.10 netmask 255.255.255.0
ifconfig $ifname_phys down

#
# Setup NVME
#
VAR_LIB_DOCKER_NS=2 # nvme namespace containing the filesystem for /var/lib/docker
service docker stop
if grep -q /var/lib/docker /proc/mounts; then
  umount /var/lib/docker
  # detach from primary, attach to first secondary controller
  if nvme detach-ns /dev/nvme0 -n $VAR_LIB_DOCKER_NS -c 0x41; then
    nvme attach-ns /dev/nvme0 -n $VAR_LIB_DOCKER_NS -c 1
  fi
fi

vfnid=$(setup_sriov_nvme $SRIOV_NVME_PF 0)

# Unbind the PF, and then bind the VF
echo nvme > /sys/bus/pci/devices/$vfnid/driver_override
echo $SRIOV_NVME_PF > /sys/bus/pci/drivers/nvme/unbind

sleep 10 # XXX: why?

echo $vfnid > /sys/bus/pci/drivers_probe

sleep 1

mount /var/lib/docker

service docker start
