#!/bin/bash -e

IMAGE_URL=https://cloud-images.ubuntu.com/releases/jammy/release/ubuntu-22.04-server-cloudimg-amd64-root.tar.xz

# This script partitions and formats a block device, then installs and configures an Ubuntu cloud image.
if [[ $# != 1 || ! -b "$1" ]]; then
    echo "Usage: $0 <block device>" > /dev/stderr
    exit 1
fi

dev=$1

resdir=$(dirname -- "$BASH_SOURCE")
cloudcfg="$resdir/cloud.cfg"
if [[ ! -f "$cloudcfg" ]]; then
    echo "Error: can't find cloud.cfg" > /dev/stderr
    exit 1
fi

extramodules="$resdir/modules.tar.xz"
if [[ ! -f "$extramodules" ]]; then
    echo "Error: can't find modules.tar.xz" > /dev/stderr
    exit 1
fi

image=$(basename "$IMAGE_URL")

# Create a GPT with a single Linux root partition covering the entire volume
sfdisk $dev <<END
label: gpt
type=4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709
END

partdev="$dev"p1

if [[ ! -b $partdev ]]; then
    echo "Error: after partitioning $dev, $partdev does not exist"
    exit 1
fi

mountpoint=$(mktemp -d)

set -x

mkfs.ext4 -L cloudimg-rootfs $partdev

mount $partdev $mountpoint

# allow for partial downloads
if [[ -f "$image" ]]; then
  curlopt="-C -"
else
  curlopt=""
fi

curl "$IMAGE_URL" $curlopt -o "$resdir/$image"

tar -xJ -C "$mountpoint" < "$resdir/$image"

tar -xJf "$extramodules" -C "$mountpoint/lib/modules"

cp "$cloudcfg" $mountpoint/etc/cloud/cloud.cfg.d/10_local.cfg

umount $mountpoint
rmdir $mountpoint
