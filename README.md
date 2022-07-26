# Core Slicing on x86

This repo contains the x86 Core Slicing prototype, excluding modifications to the Linux kernel that
are in a separate respository, including a privileged program (`runslice`) that runs on the host OS
to launch a slice, and a set of wrapper scripts for creating and running slice images and comparable
VMs.

## Hardware requirements

You will need:
 * A modern multi-core PC. We have tested only on recent HP systems with Intel CPUs (Z240 and Z8).
 * One or more PCIe UARTs, to serve as the serial console for each slice.
 * A PCIe SR-IOV network card. We used Mellanox ConnectX-4 cards.
 * A PCIe SR-IOV NVMe controller (with virtual function support -- still relatively rare at the time
   of writing). We used a Samsung PM1735 controller.

We disabled HyperThreading and turbo boost (in the BIOS) for evaluation purposes.

## System setup and boot options

Install Ubuntu 22.04 on the host as normal.

Then, edit the bootloader settings in `/etc/default/grub`, to restrict cores and memory for the
host OS, by appending these lines to the end of the file:

```
# base config, use for full system (VM host config)
GRUB_CMDLINE_LINUX_DEFAULT="memory_corruption_check=0 pci=assign-busses"

# extra config options for core slicing host
GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT maxcpus=1 mem=6G intremap=off intel_iommu=off"

# extra config for native eval (restricted CPUs/memory for native eval)
#GRUB_CMDLINE_LINUX_DEFAULT="$GRUB_CMDLINE_LINUX_DEFAULT mem=16G maxcpus=8 intremap=off intel_iommu=off"
```

Of the three statements, the top line (base config) can always be enabled. The other two lines are
either commented or uncommented depending on the desired evaluation:
 * To run the native system across all cores/memory (e.g. for hosting VMs), comment out both the
   second and third lines.
 * To run slices, uncomment the second line, and comment out the third line.
 * To run the native system with similar hardware resources to a slice (16G, 8 cores), comment-out
   the second line and uncomment the third line.

Any changes to the file will only take effect after running:
```
$ sudo update-grub && sudo reboot
```

The purpose of these kernel parameters is as follows:
 * `memory_corruption_check=0` disables a kernel feature that scans low memory for unexpected changes.
   When we boot a slice, we write our bootloader in real-mode low memory, which triggers this check.
   (Linux does not use this memory in any case, so it is harmless.)
 * `pci=assign-busses` forces the host to enumerate and assign PCI IDs to all devices.
 * `maxcpus=1 mem=6G` limits the host OS to booting on a single core and using only the first 6G of
   RAM, leaving the remaining CPUs/memory for slices.
 * `intremap=off` disables interrupt remapping using the IOAPICs and IOMMU, using exclusively
   message-signaled interrupts direct to individual local APICs.
 * `intel_iommu=off` disables the IOMMU (otherwise we'd need code in the host to setup IOMMU
   translations for slicess).

(See the [Linux kernel
documentation](https://www.kernel.org/doc/html/v5.14/admin-guide/kernel-parameters.html) for full
details.)

## Building the slice kernel

The slice kernel lives in a [separate repo](https://github.com/MSRSSP/slice-linux/tree/ubuntu-slice)
from which Ubuntu-compatible packages can be built by following [these
instructions](https://wiki.ubuntu.com/Kernel/BuildYourOwnKernel). Pre-built binary versions of these
packages are [included in the external/guest-linux folder](/external/guest-linux/).

For a fair evaluation in the native configuration, it is advisable to install the same kernel
packages on the host OS, but this is not required just to run slices.

## Slice loader

Clone this repo on the target system, and install the following build dependencies:

```
sudo apt install build-essential acpica-tools nvme-cli ninja-build python3-pip jq && pip install meson
```

### Author guest ACPI tables

To boot a slice, we need to present it with ACPI tables that reflect the subset of hardware assigned
to it. For the specific case of the DSDT, this step is unfortunately still manual. It is your job to
construct a minimal DSDT for your target system that identifies every PCI root complex (that may
contain I/O devices to be assigned to slices) and its assigned I/O and memory resource windows.

The best way to do this is to (a) look at the examples for our test systems ([HP Z240](/dsdt-hpz240.asl) and [HP Z8](/dsdt-hpz8.asl)), and (b) consult the host's `dmesg` output, for example:
```
$ sudo dmesg -t | grep 'root bus resource'
pci_bus 0000:00: root bus resource [io  0x0000-0x0cf7 window]
pci_bus 0000:00: root bus resource [io  0x1000-0x3fff window]
pci_bus 0000:00: root bus resource [mem 0x000c4000-0x000c7fff window]
pci_bus 0000:00: root bus resource [mem 0xfe010000-0xfe010fff window]
pci_bus 0000:00: root bus resource [mem 0x90000000-0x9d7fffff window]
pci_bus 0000:00: root bus resource [mem 0x380000000000-0x383fffffffff window]
pci_bus 0000:00: root bus resource [bus 00-13]
pci_bus 0000:14: root bus resource [io  0x4000-0x5fff window]
pci_bus 0000:14: root bus resource [mem 0x9d800000-0xaaffffff window]
pci_bus 0000:14: root bus resource [mem 0x384000000000-0x387fffffffff window]
pci_bus 0000:14: root bus resource [bus 14-1f]
pci_bus 0000:20: root bus resource [io  0x6000-0x7fff window]
pci_bus 0000:20: root bus resource [mem 0xab000000-0xb87fffff window]
pci_bus 0000:20: root bus resource [mem 0x388000000000-0x38bfffffffff window]
pci_bus 0000:20: root bus resource [bus 20-2b]
pci_bus 0000:2c: root bus resource [mem 0x000a0000-0x000bffff window]
pci_bus 0000:2c: root bus resource [io  0x8000-0x9fff window]
pci_bus 0000:2c: root bus resource [io  0x03b0-0x03bb window]
pci_bus 0000:2c: root bus resource [io  0x03c0-0x03df window]
pci_bus 0000:2c: root bus resource [mem 0xb8800000-0xc5ffffff window]
pci_bus 0000:2c: root bus resource [mem 0x38c000000000-0x38ffffffffff window]
pci_bus 0000:2c: root bus resource [bus 2c-7f]
pci_bus 0000:80: root bus resource [mem 0xc6000000-0xd37fffff window]
pci_bus 0000:80: root bus resource [mem 0x390000000000-0x393fffffffff window]
pci_bus 0000:80: root bus resource [bus 80-8b]
pci_bus 0000:8c: root bus resource [io  0xa000-0xbfff window]
pci_bus 0000:8c: root bus resource [mem 0xd3800000-0xe0ffffff window]
pci_bus 0000:8c: root bus resource [mem 0x394000000000-0x397fffffffff window]
pci_bus 0000:8c: root bus resource [bus 8c-97]
pci_bus 0000:98: root bus resource [io  0xc000-0xdfff window]
pci_bus 0000:98: root bus resource [mem 0xe1000000-0xee7fffff window]
pci_bus 0000:98: root bus resource [mem 0x398000000000-0x39bfffffffff window]
pci_bus 0000:98: root bus resource [bus 98-a3]
pci_bus 0000:a4: root bus resource [io  0xe000-0xffff window]
pci_bus 0000:a4: root bus resource [mem 0xee800000-0xfbffffff window]
pci_bus 0000:a4: root bus resource [mem 0x39c000000000-0x39ffffffffff window]
pci_bus 0000:a4: root bus resource [bus a4-ff]
```

This system has 8 root complexes, each of which should be represented by a PNP0A08 device in the
DSDT, if any of the PCI devices on the attached buses will be assigned to slices. We will be using a
serial controller on bus 21, a NIC on bus 99, and an NVMe controller on bus 8d, so our DSDT includes
the following:
```
    // pci_bus 0000:20: root bus resource [io  0x6000-0x7fff window]
    // pci_bus 0000:20: root bus resource [mem 0xab000000-0xb87fffff window]
    // pci_bus 0000:20: root bus resource [mem 0x388000000000-0x38bfffffffff window]
    // pci_bus 0000:20: root bus resource [bus 20-2b]
    Device(PCI0)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_BBN, 0x20)
      Name(_UID, 0)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0020,             // Range Minimum
            0x002B,             // Range Maximum
            0x0000,             // Translation Offset
            0x000C)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x00006000,         // Range Minimum
            0x00007FFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00002000,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xab000000,         // Range Minimum
            0xb87fffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0d800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x0,
            0x388000000000,
            0x38bfffffffff,
            0x0,
            0x4000000000
            ,,,)
      })
    }

    // pci_bus 0000:8c: root bus resource [io  0xa000-0xbfff window]
    // pci_bus 0000:8c: root bus resource [mem 0xd3800000-0xe0ffffff window]
    // pci_bus 0000:8c: root bus resource [mem 0x394000000000-0x397fffffffff window]
    // pci_bus 0000:8c: root bus resource [bus 8c-97]
    Device(PCI1)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_BBN, 0x8c)
      Name(_UID, 1)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x008C,             // Range Minimum
            0x0097,             // Range Maximum
            0x0000,             // Translation Offset
            0x000c)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x0000A000,         // Range Minimum
            0x0000BFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00002000,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xd3800000,         // Range Minimum
            0xe0ffffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0d800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x0,
            0x394000000000,
            0x397fffffffff,
            0x0,
            0x4000000000
            ,,,)
      })
    }

    // pci_bus 0000:98: root bus resource [io  0xc000-0xdfff window]
    // pci_bus 0000:98: root bus resource [mem 0xe1000000-0xee7fffff window]
    // pci_bus 0000:98: root bus resource [mem 0x398000000000-0x39bfffffffff window]
    // pci_bus 0000:98: root bus resource [bus 98-a3]
    Device(PCI2)
    {
      Name(_HID, EisaId("PNP0A08"))
      Name(_CID, EisaId("PNP0A03"))
      Name(_BBN, 0x98)
      Name(_UID, 2)
      Name(_CRS, ResourceTemplate() {
        WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
            0x0000,             // Granularity
            0x0098,             // Range Minimum
            0x00A3,             // Range Maximum
            0x0000,             // Translation Offset
            0x000c)             // Length
        WordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode, EntireRange,
            0x00000000,         // Granularity
            0x0000C000,         // Range Minimum
            0x0000DFFF,         // Range Maximum
            0x00000000,         // Translation Offset
            0x00002000,         // Length
            ,, , TypeStatic, DenseTranslation)
        DWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x00000000,         // Granularity
            0xe1000000,         // Range Minimum
            0xee7fffff,         // Range Maximum
            0x00000000,         // Translation Offset
            0x0d800000,         // Length
            ,, , AddressRangeMemory, TypeStatic)
        ExtendedMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite,
            0x0,
            0x398000000000,
            0x39bfffffffff,
            0x0,
            0x4000000000
            ,,,)
      })
    }
```

Create or modify`dsdt.asl` for your system as appropriate (note that the version in the repo is a
symlink).

### Building runslice

Configure the project (once only) with:
```
meson builddir
```

Then to build, run:
```
ninja -C builddir
```

NB: the shell scripts in the next step assume the build directory is named `builddir` as above.

## Running a slice

Note: all of the shell scripts here (a) must be run as root (using sudo), and (b) assume that the
current working directory is the root of the repository (i.e. they run from `./`).

### Configuration and image prep

Edit `run_config.sh` to include the PCI addresses of the available serial ports, NIC and NVMe
controller.

Edit `cloud.cfg` as desired to configure the guest slice; at a minimum, you will want to add an SSH
key to the `ssh_authorized_keys` section.

Now, it is necessary to prepare a guest image to boot. The script `mkns.sh` (and its helper,
`mkimage.sh`) automates this by:
 * constructing an NVMe namespace (with its own block device)
 * attaching it to the host
 * writing a partition table, and a filesystem within it
 * downloading and installing an Ubuntu image
 * installing our custom kernel/module/tool packages
 * configuring the cloud-init system that runs on boot
 * unmounting the image, detaching it from the host, and attaching it to the appropriate
   secondary controller (virtual function) that will be accessible to the slice

If you want to run a slice on the first virtual function, your NVMe controller is accessible on the
host as `/dev/nvme0`, and already has sufficient free space, it should be sufficient to run:
```
sudo ./mkns.sh -v 0
```
You will need to do this once for each virtual function that you wish to use (`-v 1` etc.).

Note that as a side-effect of installing an image, the scripts also extract the guest kernel
`vmlinuz` and `initrd.img` in the current directory. These are used by subsequent scripts to
boot the guest, so don't delete them.

### Slice boot

To boot a slice, the machine needs to be in the slice configuration, and the cores/memory
to be used by that slice must not already be running another slice (even if that slice has crashed
or stopped; specifically, the cores must be in the wait for IPI state). Since the only way to
stop a slice is to reset the entire system, you can expect lots of rebooting!

Although the underlying `runslice` loader can run on arbitrary cores and memory, `runslice.sh`
makes some assumptions that may be inappropriate for your situation:

```
# Physical base of memory on target NUMA node
# This is derived from ACPI SRAT (sudo acpidump -n SRAT -b && iasl -d srat.dat)
NUMA1_PHYS_BASE=0x880000000

# First core ID on target NUMA node
NUMA1_CORE_BASE=12

# Base address of RAM for this slice
# XXX: this assumes that all slices will have the same memory size, and live on the same NUMA node
RAM_PHYS_BASE=$((NUMA1_PHYS_BASE + SRIOV_VF * MEM_GB * 0x40000000))

# Starting core for this slice
# XXX: same assumption as above
CORE_BASE=$((NUMA1_CORE_BASE + SRIOV_VF * CPUS))
```

You probably want to adjust this for your system.

Having done so, you can boot a slice using the first virtual function with a command such as:
```
sudo ./runslice.sh -v 0
```

See `runslice.sh -h` for some minimal help on the parameters.

## Evaluating against VMs and native execution

The script `runvm.sh` is similar to `runslice.sh`, but runs a VM on the host using QEMU and KVM,
taking care to pin cores and memory to the same NUMA node.

The native baseline is a little more complex. First, it is necessary to use the same kernel image,
cores and memory as in a slice (see boot options above). Second, it is necessary to use virtual
functions for both NVMe and NIC (since, at least for our NVMe device, the host controller has more
queues and thus can achieve higher IOPS than a virtual function). The script `setup_native_eval.sh`
can be run on the host to mimic the guest configuration of these devices.

## Notes on NVMe configuration

The `nvme` utility is unforgiving, and poorly documented. Here's some tips:

 * Documentation: the [NVMe spec](https://nvmexpress.org/developers/nvme-specification/) is the
   best reference (especially for all the acronyms), but see also:
   * https://nvmexpress.org/resources/nvm-express-technology-features/nvme-namespaces/
   * https://stackoverflow.com/questions/65350988/how-to-setup-sr-iov-with-samsung-pm1733-1735-nvme-ssd
   * https://www.ibm.com/docs/en/linux-on-systems?topic=drive-deleting-stray-nvme-namespaces-nvme
 * At least on the PM1735, it is necessary to enable the total number of VFs in sysfs, otherwise no
   secondary controllers (even the enabled ones) can be successfully brought online.
 * Namespaces assigned to secondary controllers effectively disappear from the host. They do not
   appear in `nvme list`, nor in `nvme list-ns`. They do appear in `nvme list-ns --all`, but even so
   `nvme id-ns` will return all zeros.
 * Creating and attaching a namespace happens asynchronously -- see the delay loop in [mkns.sh](/mkns.sh).
