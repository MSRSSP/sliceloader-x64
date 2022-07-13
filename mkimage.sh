#!/bin/bash -e

# base image
IMAGE_URL=https://cloud-images.ubuntu.com/releases/jammy/release-20220712/ubuntu-22.04-server-cloudimg-amd64-root.tar.xz

# local packages to install on top
EXTRA_PACKAGES=( \
    linux-hwe-5.17-tools-5.17.0-8_5.17.0-8.8~22.04.5_amd64.deb \
    linux-hwe-5.17-tools-common_5.17.0-8.8~22.04.5_all.deb \
    linux-image-unsigned-5.17.0-8-generic_5.17.0-8.8~22.04.5_amd64.deb \
    linux-modules-5.17.0-8-generic_5.17.0-8.8~22.04.5_amd64.deb \
    linux-modules-extra-5.17.0-8-generic_5.17.0-8.8~22.04.5_amd64.deb \
    linux-tools-5.17.0-8-generic_5.17.0-8.8~22.04.5_amd64.deb )

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

declare -a extra_packages
for p in "${EXTRA_PACKAGES[@]}"; do
    pkg="$resdir/external/guest-linux/$p"
    if [[ ! -f "$pkg" ]]; then
        echo "Error: can't find $pkg" > /dev/stderr
        exit 1
    fi
    extra_packages+=("$pkg")
done

# get image, allowing to resume partial downloads
image=/tmp/$(basename "$IMAGE_URL")
if [[ -f "$image" ]]; then
  curlopt="-C -"
else
  curlopt=""
fi

curl "$IMAGE_URL" $curlopt -o "$image"


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

tar -xJ -C "$mountpoint" < "$image"

# setup a chroot for postinst scripts
for fs in proc dev sys; do
    mount --bind /$fs "$mountpoint/$fs"
done

# install our kernel and module packages
dpkg --root="$mountpoint" --force-depends --install ${extra_packages[@]}

# install missing dependencies (this needs DNS to work)
mv "$mountpoint/etc/resolv.conf" "$mountpoint/etc/resolv.conf.bak"
cp -Lv /etc/resolv.conf "$mountpoint/etc/resolv.conf"
chroot "$mountpoint" apt-get install --fix-broken --yes
mv "$mountpoint/etc/resolv.conf.bak" "$mountpoint/etc/resolv.conf"

for fs in proc dev sys; do
    umount "$mountpoint/$fs"
done

cp "$cloudcfg" $mountpoint/etc/cloud/cloud.cfg.d/10_local.cfg

# capture a copy of the kernel image and initrd -- we'll need these to boot
cp -b $mountpoint/boot/{vmlinuz,initrd.img} .

umount $mountpoint
rmdir $mountpoint
