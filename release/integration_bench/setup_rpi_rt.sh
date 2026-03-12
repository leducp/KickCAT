#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

source "$REPO_ROOT/scripts/lib/log.sh"

is_removable() {
    local dev="$1"
    local rm tran type

    read -r rm tran type < <(lsblk -dnro RM,TRAN,TYPE "$dev" 2>/dev/null) || return 1

    if [ "$type" != "disk" ]; then
        return 1
    fi
    if [ "$rm" = "1" ] || [ "$tran" = "usb" ] || [ "$tran" = "mmc" ]; then
        return 0
    fi
    return 1
}

detect_sd_card() {
    local candidates=()
    local lines

    lines=$(lsblk -dnrpo NAME,RM,TRAN,SIZE,MODEL,TYPE 2>/dev/null) || true

    while IFS=' ' read -r name rm tran size model type; do
        [ "$type" = "disk" ] || continue
        [ "$rm" = "1" ] || [ "$tran" = "usb" ] || [ "$tran" = "mmc" ] || continue

        if lsblk -nro MOUNTPOINT "$name" 2>/dev/null | grep -qE '^/(boot)?$'; then
            continue
        fi

        # Decode lsblk raw escape sequences (e.g. \x20 for spaces)
        model=$(printf '%b' "${model:-(unknown)}")
        candidates+=("$name|$size|${model}|${tran:-(unknown)}")
    done <<< "$lines"

    if [ ${#candidates[@]} -eq 0 ]; then
        error "No removable devices found."
        exit 1
    fi

    step "SD card detection"
    echo ""
    printf "  ${BOLD}%-4s %-15s %-10s %-20s %-10s${RESET}\n" "#" "DEVICE" "SIZE" "MODEL" "TRANSPORT"
    printf "  %-4s %-15s %-10s %-20s %-10s\n" "---" "---------------" "----------" "--------------------" "----------"
    for i in "${!candidates[@]}"; do
        IFS='|' read -r name size model tran <<< "${candidates[$i]}"
        printf "  %-4s %-15s %-10s %-20s %-10s\n" "$((i + 1))" "$name" "$size" "$model" "$tran"
    done
    echo ""

    local choice
    while true; do
        read -rp "Select device [1-${#candidates[@]}]: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le ${#candidates[@]} ]; then
            break
        fi
        warn "Invalid selection, try again."
    done

    IFS='|' read -r SD_CARD _ _ _ <<< "${candidates[$((choice - 1))]}"
}

FORCE=false
BUILD_DIR="$SCRIPT_DIR/build"
while getopts "fd:" opt; do
    case $opt in
        f) FORCE=true ;;
        d) BUILD_DIR="$OPTARG" ;;
        *) echo "Usage: $0 [-f] [-d build_dir] [/dev/sdX]" >&2; exit 1 ;;
    esac
done
shift $((OPTIND - 1))

SD_CARD="${1:-}"

if [ -z "$SD_CARD" ]; then
    detect_sd_card
else
    if [ ! -b "$SD_CARD" ]; then
        error "$SD_CARD is not a block device"
        exit 1
    fi
    if ! is_removable "$SD_CARD"; then
        warn "$SD_CARD does not look like a removable device!"
        read -rp "Are you sure you want to continue? [y/N] " confirm
        if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
            info "Aborted."
            exit 1
        fi
    fi
fi

step "Unmounting SD card (in case auto-mounted)"
sudo umount "${SD_CARD}1" 2>/dev/null || true
sudo umount "${SD_CARD}2" 2>/dev/null || true

step "Verifying SD card contains a Raspberry Pi image"
RPI_CHECK_MNT=$(mktemp -d)
check_cleanup() { sudo umount "$RPI_CHECK_MNT" 2>/dev/null || true; rmdir "$RPI_CHECK_MNT" 2>/dev/null || true; }
trap check_cleanup EXIT

if ! sudo mount -o ro "${SD_CARD}1" "$RPI_CHECK_MNT" 2>/dev/null; then
    error "Failed to mount ${SD_CARD}1 — is the SD card partitioned and flashed with Raspbian?"
    exit 1
fi
if [ ! -f "$RPI_CHECK_MNT/kernel8.img" ] || [ ! -f "$RPI_CHECK_MNT/cmdline.txt" ]; then
    error "Boot partition does not look like a Raspberry Pi image (missing kernel8.img or cmdline.txt)."
    exit 1
fi
success "Valid Raspberry Pi boot partition detected."
sudo umount "$RPI_CHECK_MNT"
rmdir "$RPI_CHECK_MNT"

echo ""
printf "${BOLD}KickCAT - Raspberry Pi RT Setup${RESET}\n"
printf "${BOLD}================================${RESET}\n"
echo ""
info "SD card   : $SD_CARD"
info "Build dir : $BUILD_DIR"
info "Force     : $FORCE"

RPI_LINUX_PATH="$BUILD_DIR/rpi_linux"
RPI_MNT_PATH="$BUILD_DIR/mnt"

RPI_BOOTFS="$RPI_MNT_PATH/boot"
RPI_ROOTFS="$RPI_MNT_PATH/root"

cleanup() {
    sudo umount "$RPI_BOOTFS" 2>/dev/null || true
    sudo umount "$RPI_ROOTFS" 2>/dev/null || true
}
trap cleanup EXIT

if [ "$FORCE" = true ] && [ -d "$RPI_LINUX_PATH" ]; then
    step "Cleaning build directory (force mode)"
    rm -rf "$RPI_LINUX_PATH"
    success "Build directory cleaned."
fi

step "Fetching kernel sources"
if [ ! -d "$RPI_LINUX_PATH/.git" ]; then
    info "Cloning kernel sources..."
    mkdir -p "$RPI_LINUX_PATH"
    git clone --depth=1 https://github.com/raspberrypi/linux "$RPI_LINUX_PATH"
    success "Kernel sources cloned."
else
    success "Kernel sources already present, skipping clone."
fi

step "Configuring kernel"
KERNEL=kernel8
cp "$SCRIPT_DIR/bcm2711_defconfig" "$RPI_LINUX_PATH/arch/arm64/configs/bcm2711_defconfig"

if [ "$FORCE" = true ] || [ ! -f "$RPI_LINUX_PATH/.config" ]; then
    info "Generating kernel config (bcm2711_defconfig)..."
    make -C "$RPI_LINUX_PATH" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- bcm2711_defconfig
    success "Kernel config generated."
else
    success "Kernel config already present, skipping defconfig."
fi

step "Building kernel ($(nproc) jobs)"
info "Building Image, modules, and device trees..."
make -j "$(nproc)" -C "$RPI_LINUX_PATH" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules dtbs
success "Kernel build complete."

step "Installing to SD card ($SD_CARD)"
mkdir -p "$RPI_BOOTFS" "$RPI_ROOTFS"

info "Mounting ${SD_CARD}1 -> $RPI_BOOTFS"
if ! sudo mount -o rw "${SD_CARD}1" "$RPI_BOOTFS"; then
    error "Failed to mount boot partition read-write. Try: sudo fsck.vfat -a ${SD_CARD}1"
    exit 1
fi
info "Mounting ${SD_CARD}2 -> $RPI_ROOTFS"
sudo mount "${SD_CARD}2" "$RPI_ROOTFS"

info "Installing kernel modules..."
sudo env "PATH=$PATH" make -j "$(nproc)" -C "$RPI_LINUX_PATH" ARCH=arm64 "CROSS_COMPILE=aarch64-linux-gnu-" INSTALL_MOD_PATH="$RPI_ROOTFS" modules_install
success "Kernel modules installed."

info "Backing up existing kernel image..."
sudo cp "$RPI_BOOTFS/$KERNEL.img" "$RPI_BOOTFS/$KERNEL-backup.img"

info "Copying kernel image and device trees..."
sudo cp "$RPI_LINUX_PATH/arch/arm64/boot/Image" "$RPI_BOOTFS/$KERNEL.img"
sudo cp "$RPI_LINUX_PATH/arch/arm64/boot/dts/broadcom/"*.dtb "$RPI_BOOTFS/"
sudo cp "$RPI_LINUX_PATH/arch/arm64/boot/dts/overlays/"*.dtb* "$RPI_BOOTFS/overlays/"
sudo cp "$RPI_LINUX_PATH/arch/arm64/boot/dts/overlays/README" "$RPI_BOOTFS/overlays/"
success "Kernel and device trees installed."

step "Configuring RT system settings"

info "Deploying rootfs overlay (sysctl, limits, systemd units)..."
sudo cp -r "$SCRIPT_DIR/rootfs_overlay/"* "$RPI_ROOTFS/"
success "Rootfs overlay deployed."

info "Enabling systemd services..."
sudo mkdir -p "$RPI_ROOTFS/etc/systemd/system/multi-user.target.wants"
sudo ln -sf /etc/systemd/system/cpu-performance.service "$RPI_ROOTFS/etc/systemd/system/multi-user.target.wants/cpu-performance.service"
sudo ln -sf /etc/systemd/system/kickcat-user-boot.service "$RPI_ROOTFS/etc/systemd/system/multi-user.target.wants/kickcat-user-boot.service"
success "Services enabled."

RT_CMDLINE="isolcpus=1-3 nohz_full=1-3 rcu_nocbs=1-3"
if grep -q "isolcpus" "$RPI_BOOTFS/cmdline.txt"; then
    success "Kernel cmdline already contains isolcpus, skipping."
else
    info "Patching cmdline.txt with: $RT_CMDLINE"
    sudo sed -i "s/$/ ${RT_CMDLINE}/" "$RPI_BOOTFS/cmdline.txt"
    success "cmdline.txt patched."
fi

info "Unmounting partitions..."
sudo umount "$RPI_BOOTFS"
sudo umount "$RPI_ROOTFS"
success "Partitions unmounted."

echo ""
printf "${GREEN}${BOLD}Done!${RESET} SD card is ready.\n"
echo ""
info "RT configuration summary:"
info "  - RT throttling disabled (sched_rt_runtime_us=-1)"
info "  - CPU governor set to performance"
info "  - Cores 1-3 isolated (isolcpus, nohz_full, rcu_nocbs)"
info "  - User 'pi' can run RT threads up to priority 90"
info "  - /home/pi/boot.sh will be executed at boot (if present)"
echo ""
