# Integration Test Bench - Raspberry Pi RT Setup

This script cross-compiles a custom Linux kernel for the Raspberry Pi 3/4 (BCM2711) and configures the system for real-time operation, tailored for the KickCAT integration test bench. It enables **PREEMPT_RT** (real-time scheduling) and **XDP sockets** support.

## Prerequisites

- **SD card pre-flashed with Raspbian Lite** — the script installs a custom kernel onto an existing Raspbian installation. It does not create a bootable image from scratch.
- Cross-compilation toolchain and build dependencies as described in the [Raspberry Pi cross compile the kernel documentation](https://www.raspberrypi.com/documentation/computers/linux_kernel.html#cross-compile-the-kernel).

## Usage

```
./setup_rpi_rt.sh [-f] [-d build_dir] [/dev/sdX]
```

- `/dev/sdX` — SD card block device (optional: auto-detected if omitted).
- `-d build_dir` — kernel build directory (optional: defaults to `./build`). Kept between runs for incremental builds.
- `-f` — force a clean rebuild from scratch (re-clone sources, regenerate config).

### Examples

```bash
# Auto-detect SD card, incremental build
./setup_rpi_rt.sh

# Specify SD card explicitly
./setup_rpi_rt.sh /dev/sdb

# Custom build directory, auto-detect SD card
./setup_rpi_rt.sh -d /tmp/kernel_build

# Full clean rebuild
./setup_rpi_rt.sh -f /dev/sdb
```

## What it does

1. Clones the Raspberry Pi kernel sources (skipped if already present).
2. Applies the `bcm2711_defconfig` with PREEMPT_RT and XDP patches.
3. Cross-compiles the kernel image, modules, and device trees.
4. Mounts the SD card partitions and installs the built artifacts (backing up the original kernel).
5. Deploys RT system configuration:
   - Disables RT throttling (`kernel.sched_rt_runtime_us=-1`).
   - Sets the CPU governor to `performance`.
   - Isolates cores 1-3 for RT workloads (`isolcpus`, `nohz_full`, `rcu_nocbs`).
   - Grants user `pi` RT thread priorities up to 90 and unlimited memlock.
   - Runs `/home/pi/boot.sh` at boot (if present) for user applications.
   - Pins `raspberrypi-kernel` and `raspberrypi-bootloader` packages to prevent `apt upgrade` from overwriting the custom kernel.
