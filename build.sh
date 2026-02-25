#!/bin/bash
#
# CITADEL Build Script
# Builds the kernel and creates a bootable ISO
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Project directories
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
ISO_DIR="${PROJECT_DIR}/iso"
LIMINE_DIR="${PROJECT_DIR}/limine"
RAMDISK_DIR="${PROJECT_DIR}/ramdisk"
SHARED_DIR="${PROJECT_DIR}/shared"

# Output files
KERNEL_ELF="${BUILD_DIR}/kernel/citadel.elf"
ISO_FILE="${BUILD_DIR}/citadel-limine.iso"
RAMDISK_OUTPUT="${ISO_DIR}/modules/ramdisk.img"
SERIAL_LOG="${BUILD_DIR}/serial.log"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}       CITADEL Build System${NC}"
echo -e "${CYAN}========================================${NC}"

# Parse arguments
CLEAN=false
VERBOSE=false
RUN_QEMU=false
FULLSCREEN=false
TPM=false
USE_TABLET=true
JOBS=$(nproc 2>/dev/null || echo 4)

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -r|--run)
            RUN_QEMU=true
            shift
            ;;
        -f|--fullscreen)
            FULLSCREEN=true
            shift
            ;;
        --tpm)
            TPM=true
            shift
            ;;
        --tablet)
            USE_TABLET=true
            shift
            ;;
        --relmouse)
            # Force relative USB mouse (can cause grab/misalignment in some QEMU setups).
            USE_TABLET=false
            shift
            ;;
        -j*)
            JOBS="${1#-j}"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  -c, --clean     Clean build (remove build directory)"
            echo "  -v, --verbose   Verbose build output"
            echo "  -r, --run       Run in QEMU after building"
            echo "  -f, --fullscreen Start QEMU fullscreen"
            echo "  --tpm           Enable TPM2 emulation (requires swtpm)"
            echo "  --tablet        Use absolute USB tablet in QEMU (default)"
            echo "  --relmouse      Use relative USB mouse in QEMU"
            echo "  -j<N>           Use N parallel jobs (default: $(nproc))"
            echo "  -h, --help      Show this help"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Step 1: Clean if requested
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}[1/5] Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
    echo -e "${GREEN}      Done.${NC}"
else
    echo -e "${YELLOW}[1/5] Skipping clean (use -c to clean)${NC}"
fi

# Step 2: Create build directory and configure
echo -e "${YELLOW}[2/5] Configuring with CMake...${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "Makefile" ] || [ "$CLEAN" = true ]; then
    cmake .. || { echo -e "${RED}CMake configuration failed!${NC}"; exit 1; }
fi
echo -e "${GREEN}      Done.${NC}"

# Step 3: Build
echo -e "${YELLOW}[3/5] Building kernel (using ${JOBS} jobs)...${NC}"
if [ "$VERBOSE" = true ]; then
    make -j${JOBS} VERBOSE=1 || { echo -e "${RED}Build failed!${NC}"; exit 1; }
else
    make -j${JOBS} || { echo -e "${RED}Build failed!${NC}"; exit 1; }
fi
echo -e "${GREEN}      Done.${NC}"

# Step 4: Copy kernel to ISO directory
echo -e "${YELLOW}[4/5] Preparing ISO contents...${NC}"
if [ ! -f "${KERNEL_ELF}" ]; then
    echo -e "${RED}Kernel ELF not found at ${KERNEL_ELF}${NC}"
    exit 1
fi
cp "${KERNEL_ELF}" "${ISO_DIR}/boot/"
echo -e "${GREEN}      Copied kernel to iso/boot/${NC}"

if [ -d "${RAMDISK_DIR}" ]; then
    echo -e "${YELLOW}      Building ramdisk image...${NC}"
    mkdir -p "${ISO_DIR}/modules"
    RAMDISK_TEMP="${BUILD_DIR}/ramdisk.img"
    dd if=/dev/zero of="${RAMDISK_TEMP}" bs=1M count=0 seek=4 >/dev/null 2>&1
    mkfs.fat -F 32 -n CITADELRD "${RAMDISK_TEMP}" >/dev/null 2>&1
    if compgen -G "${RAMDISK_DIR}/*" >/dev/null; then
        mcopy -s -i "${RAMDISK_TEMP}" ${RAMDISK_DIR}/* :: >/dev/null 2>&1
    fi

    # Also include the project-root desktop.json (if present)
    if [ -f "${PROJECT_DIR}/desktop.json" ]; then
        # Copy as an 8.3 name so our current FAT32 implementation (no LFN) can open it reliably.
        # Keep the source name desktop.json, but store it in the image as DESKTOP.JSN.
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop.json" ::/DESKTOP.JSN >/dev/null 2>&1
    fi

    # Include startup.cfg so kernel startup-mode parsing can work from the ramdisk
    if [ -f "${PROJECT_DIR}/startup.cfg" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/startup.cfg" ::/STARTUP.CFG >/dev/null 2>&1
    fi
    cp "${RAMDISK_TEMP}" "${RAMDISK_OUTPUT}"
    echo -e "${GREEN}      Ramdisk written to modules/ramdisk.img${NC}"
fi

# Step 5: Create ISO
echo -e "${YELLOW}[5/5] Creating bootable ISO...${NC}"
cd "${PROJECT_DIR}"

# Ensure Limine boot binaries exist in the ISO tree.
# These files are typically provided by the `limine` submodule and are ignored by git in the ISO staging folder.
mkdir -p "${ISO_DIR}/boot/limine"
for f in limine-bios-cd.bin limine-bios.sys limine-uefi-cd.bin; do
    if [ ! -f "${ISO_DIR}/boot/limine/${f}" ]; then
        if [ -f "${LIMINE_DIR}/${f}" ]; then
            cp "${LIMINE_DIR}/${f}" "${ISO_DIR}/boot/limine/${f}"
        else
            echo -e "${RED}Missing Limine boot file: ${f}${NC}"
            echo -e "${RED}Hint: run 'git submodule update --init --recursive' to populate limine.${NC}"
            exit 1
        fi
    fi
done

# Check for xorriso
if ! command -v xorriso &> /dev/null; then
    echo -e "${RED}xorriso not found! Install with: sudo apt install xorriso${NC}"
    exit 1
fi

xorriso -as mkisofs \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    --protective-msdos-label \
    "${ISO_DIR}" \
    -o "${ISO_FILE}" \
    2>&1 | grep -v "^xorriso" || true

# Install Limine BIOS stages (optional, may fail on CD image)
if [ -x "${LIMINE_DIR}/limine" ]; then
    "${LIMINE_DIR}/limine" bios-install "${ISO_FILE}" 2>/dev/null || true
fi

echo -e "${GREEN}      Done.${NC}"

# Summary
echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${GREEN}Build complete!${NC}"
echo -e "${CYAN}========================================${NC}"
echo -e "  Kernel: ${KERNEL_ELF}"
echo -e "  ISO:    ${ISO_FILE}"
echo -e "  Size:   $(du -h "${ISO_FILE}" | cut -f1)"
echo ""

# Run QEMU if requested
if [ "$RUN_QEMU" = true ]; then
    echo -e "${YELLOW}Launching QEMU...${NC}"
    echo -e "${CYAN}(Click inside QEMU window for keyboard focus)${NC}"
    echo -e "${CYAN}(Press Ctrl+Q in OS to shutdown)${NC}"
    echo -e "${CYAN}(Serial log: ${SERIAL_LOG})${NC}"
    echo ""

    # Start each run with a clean serial log.
    : > "${SERIAL_LOG}"

    # Optional TPM2 emulation via swtpm
    SWTPM_PID=""
    TPM_ARGS=()
    if [ "$TPM" = true ]; then
        if ! command -v swtpm &> /dev/null; then
            echo -e "${RED}swtpm not found. Install with: sudo apt install swtpm swtpm-tools${NC}"
            exit 1
        fi

        SWTPM_DIR="${BUILD_DIR}/swtpm"
        SWTPM_SOCK="${SWTPM_DIR}/swtpm-sock"
        mkdir -p "${SWTPM_DIR}/state"
        rm -f "${SWTPM_SOCK}"

        echo -e "${GREEN}Starting swtpm (TPM2) at ${SWTPM_SOCK}${NC}"
        swtpm socket --tpm2 \
            --tpmstate dir="${SWTPM_DIR}/state" \
            --ctrl type=unixio,path="${SWTPM_SOCK}" \
            --log level=20 \
            >/dev/null 2>&1 &
        SWTPM_PID=$!

        cleanup_swtpm() {
            if [ -n "${SWTPM_PID}" ] && kill -0 "${SWTPM_PID}" 2>/dev/null; then
                kill "${SWTPM_PID}" 2>/dev/null || true
                wait "${SWTPM_PID}" 2>/dev/null || true
            fi
            rm -f "${SWTPM_SOCK}" 2>/dev/null || true
        }
        trap cleanup_swtpm EXIT

        TPM_ARGS=(
            -chardev "socket,id=chrtpm,path=${SWTPM_SOCK}"
            -tpmdev "emulator,id=tpm0,chardev=chrtpm"
            -device "tpm-crb,tpmdev=tpm0"
        )
    fi

    SHARED_ARGS=()
    if [ -d "${SHARED_DIR}" ]; then
        echo -e "${GREEN}Mounting shared folder at ${SHARED_DIR}${NC}"
        SHARED_ARGS=(-drive "file=fat:rw:${SHARED_DIR},format=raw,if=ide,index=1")
    else
        echo -e "${YELLOW}Shared folder not found at ${SHARED_DIR}; skipping host share (mkdir shared to enable).${NC}"
    fi

    # Use xHCI controller with a USB tablet (absolute) by default.
    # This avoids QEMU relative-mouse grab artifacts and improves 1:1 UI hit-testing.
    INPUT_DEVICE=( -device usb-tablet,bus=xhci.0 )
    if [ "$USE_TABLET" = false ]; then
        INPUT_DEVICE=( -device usb-mouse,bus=xhci.0 )
    fi

    QEMU_ARGS=(
        -cdrom "${ISO_FILE}"
        -boot order=d
        -m 256M
        -vga vmware
        -netdev user,id=net0
        -device e1000,netdev=net0
        # Allow the guest to terminate QEMU cleanly via I/O port 0xF4.
        -device isa-debug-exit,iobase=0xf4,iosize=0x04
        -device qemu-xhci,id=xhci
        "${INPUT_DEVICE[@]}"
        -serial file:"${SERIAL_LOG}"
    )
    if [ "$FULLSCREEN" = true ]; then
        QEMU_ARGS+=( -full-screen )
    fi

    # Prefer distro QEMU on Ubuntu/WSL. Some custom /usr/local builds omit
    # user-mode networking (slirp), which breaks `-netdev user`.
    QEMU_BIN="qemu-system-x86_64"
    if [ -x "/usr/bin/qemu-system-x86_64" ]; then
        QEMU_BIN="/usr/bin/qemu-system-x86_64"
    elif [ -x "/usr/local/bin/qemu-system-x86_64" ]; then
        QEMU_BIN="/usr/local/bin/qemu-system-x86_64"
    fi
    echo -e "${CYAN}Using QEMU: ${QEMU_BIN}${NC}"

    #QEMU_CMD = "qemu-system-x86_64 "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" ${SHARED_ARGS[@]}  
    echo "${QEMU_BIN} ${QEMU_ARGS[@]} ${TPM_ARGS[@]} ${SHARED_ARGS[@]}"
    "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" ${SHARED_ARGS[@]}
    echo ""
    echo -e "${CYAN}=== Serial output (last 30 lines) ===${NC}"
    tail -30 "${SERIAL_LOG}"
fi
