#!/bin/bash -e

# This script creates an NVMe namespace, attaches it to the host, formats it with a new image, then
# attaches it to a specified secondary controller.

# defaults
NVME_DEV=/dev/nvme0
SIZE_GB=512
BLOCK_SIZE=4096
VFNID=-1

# Parse arguments
while [[ $# -gt 0 ]]
do
  case "$1" in
  -d)
    NVME_DEV="$2"
    shift
    ;;

  -g)
    SIZE_GB="$2"
    shift
    ;;

  -b)
    BLOCK_SIZE="$2"
    shift
    ;;

  -v)
    VFNID="$2"
    shift
    ;;

  -h)
    echo "Usage: $0 [args]"
    echo "   -d DEV       host NVME device"
    echo "   -g GB        namespace size in GB"
    echo "   -b BYTES     block size"
    echo "   -v VFNID     (0-based) virtual function ID to which to attach the namespace"
    exit 0
    ;;

  *)
    echo "Error: unknown argument: $1"
    exit 1
    ;;
  esac

  shift
done

if [[ ! -c "$NVME_DEV" ]]; then
    echo "Error: $NVME_DEV is not a char device" > /dev/stderr
    exit 1
fi

if [[ "$VFNID" -lt 0 ]]; then
    echo "Error: -v argument is required" > /dev/stderr
    exit 1
fi

# Get the ID of the primary (host) controller
HOST_CNTLID=$(nvme id-ctrl $NVME_DEV -o json | jq .cntlid)

# Get the ID of the secondary (virtual function) controller
VIRT_CNTLID=$(nvme list-secondary /dev/nvme0 -o json | jq '."secondary-controllers"[]|select(."virtual-function-number"=='$((VFNID + 1))')."secondary-controller-identifier"')

SIZE_BLOCKS=$((SIZE_GB * 1000000000 / BLOCK_SIZE))

echo "Primary controller ID:   $HOST_CNTLID"
echo "Secondary controller ID: $VIRT_CNTLID"
echo "Creating namespace with $SIZE_BLOCKS $BLOCK_SIZE-byte blocks..."

set -x

# Create the new namespace, and capture the output, which should be "create-ns: Success, created nsid:32"
out=$(nvme create-ns $NVME_DEV --nsze=$SIZE_BLOCKS --ncap=$SIZE_BLOCKS --block-size $BLOCK_SIZE)

set +x

NSID=${out#*created nsid:}
echo "Created namespace $NSID"

# Attach the namespace to the host
nvme attach-ns $NVME_DEV -n $NSID -c $HOST_CNTLID

# Wait for the namespace to populate
while [[ $(nvme id-ns $NVME_DEV -n $NSID -o json | jq .nsze) -eq 0 ]]; do
    echo -n .
    sleep 1
    nvme ns-rescan $NVME_DEV
done

set -x

# Figure out the host device path
NSDEV=$(nvme list -o json | jq -r ".Devices[]|select(.NameSpace==$NSID).DevicePath")

# Partition/format/image the new namespace
$(dirname "$BASH_SOURCE")/mkimage.sh $NSDEV

# Detach the new namespace from the primary controller
nvme detach-ns $NVME_DEV -c $HOST_CNTLID -n $NSID

# Attach it to the secondary controller
nvme attach-ns $NVME_DEV -c $VIRT_CNTLID -n $NSID
