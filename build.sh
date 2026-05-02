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
ARTIFACTS_DIR="${BUILD_DIR}/artifacts"
KERNEL_ARTIFACT_DIR="${ARTIFACTS_DIR}/kernel"
MODULE_BUNDLE_DIR="${ARTIFACTS_DIR}/modules"
ISO_DIR="${PROJECT_DIR}/iso"
LIMINE_DIR="${PROJECT_DIR}/limine"
RAMDISK_DIR="${PROJECT_DIR}/ramdisk"
SHARED_DIR="${PROJECT_DIR}/shared"

# Output files
KERNEL_ELF="${BUILD_DIR}/kernel/citadel.elf"
KERNEL_BIN="${BUILD_DIR}/citadel.bin"
KERNEL_ARTIFACT_ELF="${KERNEL_ARTIFACT_DIR}/citadel.elf"
KERNEL_ARTIFACT_BIN="${KERNEL_ARTIFACT_DIR}/citadel.bin"
ISO_FILE="${BUILD_DIR}/citadel-limine.iso"
MODULE_RAMDISK_IMAGE="${MODULE_BUNDLE_DIR}/ramdisk.img"
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
PRODUCTION=false
JOBS=$(nproc 2>/dev/null || echo 4)
RUN_FOR_SECONDS=0
HEADLESS=false
AUTO_GRAB=true
SYSTEM_VOL=false
RESET_SYSTEM_VOL=false

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
        --run-for)
            RUN_QEMU=true
            shift
            RUN_FOR_SECONDS="$1"
            shift
            ;;
        --headless)
            HEADLESS=true
            shift
            ;;
        --no-grab|--nograb)
            AUTO_GRAB=false
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
        --prod)
            PRODUCTION=true
            shift
            ;;
        --system-vol)
            SYSTEM_VOL=true
            shift
            ;;
        --reset-system-vol)
            RESET_SYSTEM_VOL=true
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
            echo "  --run-for <s>   Run QEMU, auto-stop after <s> seconds (best for scripted/CI runs)"
            echo "  --headless      Run QEMU without a GUI window (serial still logs to build/serial.log)"
            echo "  --no-grab       Disable QEMU grab-on-hover (by default we try to avoid first-click focus loss)"
            echo "  -f, --fullscreen Start QEMU fullscreen"
            echo "  --tpm           Enable TPM2 emulation (requires swtpm)"
            echo "  --tablet        Use absolute USB tablet in QEMU (default)"
            echo "  --relmouse      Use relative USB mouse in QEMU"
            echo "  --system-vol    Attach persistent system volume disk (build/system.qcow2)"
            echo "  --reset-system-vol Delete build/system.qcow2 before build/run"
            echo "  --prod          Build production mode (fail-closed boot signature enforcement; use --tpm when running)"
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

if [ "$RESET_SYSTEM_VOL" = true ]; then
    if [ -f "${BUILD_DIR}/system.qcow2" ]; then
        echo -e "${YELLOW}Resetting persistent system volume: ${BUILD_DIR}/system.qcow2${NC}"
        rm -f "${BUILD_DIR}/system.qcow2"
    else
        echo -e "${YELLOW}No persistent system volume found to reset.${NC}"
    fi
fi

# Helper: sign a file with the BootGate RSA private key without ever prompting.
# If the key is encrypted, set CITADEL_BOOTGATE_PASSPHRASE to allow signing.
sign_sha256_rsa2048_no_prompt() {
    local key="$1"
    local input="$2"
    local output="$3"

    if [ -z "${key}" ] || [ -z "${input}" ] || [ -z "${output}" ]; then
        return 1
    fi

    # Detect encrypted keys by PEM header/content markers.
    if grep -q "ENCRYPTED" "${key}" 2>/dev/null; then
        if [ -z "${CITADEL_BOOTGATE_PASSPHRASE}" ]; then
            return 2
        fi
        openssl dgst -sha256 -sign "${key}" -passin env:CITADEL_BOOTGATE_PASSPHRASE -out "${output}" "${input}"
        return $?
    fi

    openssl dgst -sha256 -sign "${key}" -out "${output}" "${input}"
    return $?
}

# Step 1: Clean if requested
PRESERVED_SYSTEM_DISK=""
if [ "$CLEAN" = true ]; then
    echo -e "${YELLOW}[1/5] Cleaning build directory...${NC}"
    # Preserve the persistent system volume across clean rebuilds.
    # This keeps /system SecureStore state (e.g., OWNERCRD, WRAPKEY) stable during dev.
    if [ -f "${BUILD_DIR}/system.qcow2" ]; then
        PRESERVED_SYSTEM_DISK="$(mktemp -p /tmp citadel_system_XXXXXX.qcow2 2>/dev/null || true)"
        if [ -n "${PRESERVED_SYSTEM_DISK}" ]; then
            mv "${BUILD_DIR}/system.qcow2" "${PRESERVED_SYSTEM_DISK}" 2>/dev/null || PRESERVED_SYSTEM_DISK=""
        fi
    fi

    rm -rf "${BUILD_DIR}"
    echo -e "${GREEN}      Done.${NC}"
else
    echo -e "${YELLOW}[1/5] Skipping clean (use -c to clean)${NC}"
fi

# Step 2: Create build directory and configure
echo -e "${YELLOW}[2/5] Configuring with CMake...${NC}"
mkdir -p "${BUILD_DIR}"

if [ -n "${PRESERVED_SYSTEM_DISK}" ] && [ -f "${PRESERVED_SYSTEM_DISK}" ]; then
    mv "${PRESERVED_SYSTEM_DISK}" "${BUILD_DIR}/system.qcow2" 2>/dev/null || true
    PRESERVED_SYSTEM_DISK=""
fi

cd "${BUILD_DIR}"

DESIRED_PROD="OFF"
if [ "$PRODUCTION" = true ]; then
    DESIRED_PROD="ON"
fi

NEED_CMAKE=false
if [ ! -f "Makefile" ] || [ "$CLEAN" = true ]; then
    NEED_CMAKE=true
elif [ -f "CMakeCache.txt" ]; then
    CACHED_PROD=$(grep -E '^CITADEL_PRODUCTION:BOOL=' CMakeCache.txt 2>/dev/null | cut -d= -f2)
    if [ "${CACHED_PROD}" != "${DESIRED_PROD}" ]; then
        NEED_CMAKE=true
    fi
else
    NEED_CMAKE=true
fi

if [ "$NEED_CMAKE" = true ]; then
    cmake .. -DCITADEL_PRODUCTION=${DESIRED_PROD} || { echo -e "${RED}CMake configuration failed!${NC}"; exit 1; }
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
mkdir -p "${KERNEL_ARTIFACT_DIR}" "${MODULE_BUNDLE_DIR}" "${ISO_DIR}/boot" "${ISO_DIR}/modules"
cp "${KERNEL_ELF}" "${KERNEL_ARTIFACT_ELF}"
if [ -f "${KERNEL_BIN}" ]; then
    cp "${KERNEL_BIN}" "${KERNEL_ARTIFACT_BIN}"
fi
cp "${KERNEL_ARTIFACT_ELF}" "${ISO_DIR}/boot/"
echo -e "${GREEN}      Copied kernel to iso/boot/${NC}"
echo -e "${GREEN}      Kernel artifact: ${KERNEL_ARTIFACT_ELF}${NC}"

if [ -d "${RAMDISK_DIR}" ]; then
    echo -e "${YELLOW}      Building ramdisk image...${NC}"
    RAMDISK_TEMP="${MODULE_RAMDISK_IMAGE}"
    # Size ramdisk based on source tree size. The previous fixed 4MB image can fill up
    # quickly once wallpapers/assets are added, causing silent mcopy failures.
    RAMDISK_SRC_MB=$(du -sm "${RAMDISK_DIR}" | awk '{print $1}')
    RAMDISK_MB=$((RAMDISK_SRC_MB + 8))
    if [ "${RAMDISK_MB}" -lt 16 ]; then
        RAMDISK_MB=16
    fi

    echo -e "${CYAN}      Ramdisk size: ${RAMDISK_MB}MB (src=${RAMDISK_SRC_MB}MB + 8MB)${NC}"
    RAMDISK_T0=$(date +%s)
    dd if=/dev/zero of="${RAMDISK_TEMP}" bs=1M count=0 seek="${RAMDISK_MB}" status=none
    # Use a larger cluster size to reduce FAT traversal overhead when reading large assets
    # (e.g., wallpapers). Default formatting can choose 512B clusters, which is very slow.
    mkfs.fat -F 32 -s 8 -n CITADELRD "${RAMDISK_TEMP}" >/dev/null
    if compgen -G "${RAMDISK_DIR}/*" >/dev/null; then
        echo -e "${CYAN}      Copying ramdisk/ contents into image...${NC}"
        if ! mcopy -s -i "${RAMDISK_TEMP}" ${RAMDISK_DIR}/* :: >/dev/null 2>&1; then
            echo -e "${RED}mcopy failed while populating ramdisk image (size=${RAMDISK_MB}MB).${NC}"
            echo -e "${RED}Tip: check for 'Disk full' or filename collisions under ${RAMDISK_DIR}.${NC}"
            exit 1
        fi
    fi

    # Keep a non-shadowed copy of desktop assets at the ramdisk root.
    # When a persistent /system volume is mounted, it hides the bundled /system tree,
    # so runtime fallbacks use /WALL and /ICONS to reach the boot-packaged assets.
    if [ -d "${RAMDISK_DIR}/system/wall" ]; then
        mdir -i "${RAMDISK_TEMP}" ::/WALL >/dev/null 2>&1 || mmd -i "${RAMDISK_TEMP}" ::/WALL >/dev/null 2>&1 || true
        if compgen -G "${RAMDISK_DIR}/system/wall/*" >/dev/null; then
            if ! mcopy -s -o -i "${RAMDISK_TEMP}" ${RAMDISK_DIR}/system/wall/* ::/WALL >/dev/null 2>&1; then
                echo -e "${RED}Failed to create /WALL asset aliases in ramdisk image.${NC}"
                exit 1
            fi
        fi
    fi

    if [ -d "${RAMDISK_DIR}/system/icons" ]; then
        mdir -i "${RAMDISK_TEMP}" ::/ICONS >/dev/null 2>&1 || mmd -i "${RAMDISK_TEMP}" ::/ICONS >/dev/null 2>&1 || true
        if compgen -G "${RAMDISK_DIR}/system/icons/*" >/dev/null; then
            if ! mcopy -s -o -i "${RAMDISK_TEMP}" ${RAMDISK_DIR}/system/icons/* ::/ICONS >/dev/null 2>&1; then
                echo -e "${RED}Failed to create /ICONS asset aliases in ramdisk image.${NC}"
                exit 1
            fi
        fi
    fi

    # Font aliases for FAT 8.3 (current VFS has no LFN support).
    # If you add RobotoMono static TTFs with long filenames, create a stable 8.3 alias
    # so runtime can open it reliably via "/system/fonts/RMONO.TTF".
    {
        ROBOTO_MONO_REG_SRC="${RAMDISK_DIR}/system/fonts/static/RobotoMono-Regular.ttf"
        if [ -f "${ROBOTO_MONO_REG_SRC}" ]; then
            # /system/fonts should already exist (copied from ramdisk tree). Avoid mmd here:
            # mtools can prompt on /dev/tty if the directory exists, and we redirect stderr.
            if ! mcopy -o -i "${RAMDISK_TEMP}" "${ROBOTO_MONO_REG_SRC}" ::/system/fonts/RMONO.TTF >/dev/null 2>&1; then
                echo -e "${YELLOW}WARNING: Failed to alias RobotoMono-Regular.ttf as /system/fonts/RMONO.TTF in ramdisk image${NC}"
            fi
        fi
    }

    RAMDISK_T1=$(date +%s)
    echo -e "${GREEN}      Ramdisk build done in $((RAMDISK_T1 - RAMDISK_T0))s.${NC}"

    # Also include the project-root desktop.json (if present)
    if [ -f "${PROJECT_DIR}/desktop.json" ]; then
        # Copy as an 8.3 name so our current FAT32 implementation (no LFN) can open it reliably.
        # Keep the source name desktop.json, but store it in the image as DESKTOP.JSN.
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop.json" ::/DESKTOP.JSN >/dev/null 2>&1
    fi

    # Also include the CUI-ML desktop definition (if present)
    CUI_DESKTOP_SRC=""
    if [ -f "${PROJECT_DIR}/desktop.cuiml" ]; then
        CUI_DESKTOP_SRC="${PROJECT_DIR}/desktop.cuiml"
    elif [ -f "${PROJECT_DIR}/shared/desktop.cuiml" ]; then
        CUI_DESKTOP_SRC="${PROJECT_DIR}/shared/desktop.cuiml"
    fi

    if [ -n "${CUI_DESKTOP_SRC}" ]; then
        # Stored as DESKTOP.CML (8.3) for early FAT32 reader compatibility.
        mcopy -i "${RAMDISK_TEMP}" "${CUI_DESKTOP_SRC}" ::/DESKTOP.CML >/dev/null 2>&1

        # Optional signature file for DESKTOP.CML.
        # If you provide a raw DESKTOP.SIG (RSA-2048 signature, 256 bytes) it will be packed as 8.3.
        # If you provide a signing key at keys/bootgate_rsa_priv.pem and have openssl installed,
        # we will generate DESKTOP.SIG automatically.
        DESKTOP_SIG_SRC=""
        if [ -f "${PROJECT_DIR}/DESKTOP.SIG" ]; then
            DESKTOP_SIG_SRC="${PROJECT_DIR}/DESKTOP.SIG"
        elif [ -f "${PROJECT_DIR}/desktop.sig" ]; then
            DESKTOP_SIG_SRC="${PROJECT_DIR}/desktop.sig"
        else
            BOOTGATE_PRIV_KEY=""
            if [ -f "${PROJECT_DIR}/keys/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${PROJECT_DIR}/keys/bootgate_rsa_priv.pem"
            elif [ -f "${BUILD_DIR}/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${BUILD_DIR}/bootgate_rsa_priv.pem"
            fi

            if command -v openssl >/dev/null 2>&1 && [ -n "${BOOTGATE_PRIV_KEY}" ]; then
                DESKTOP_SIG_GEN="${BUILD_DIR}/DESKTOP.SIG"
                sign_sha256_rsa2048_no_prompt "${BOOTGATE_PRIV_KEY}" "${CUI_DESKTOP_SRC}" "${DESKTOP_SIG_GEN}" >/dev/null 2>&1 || true
                if [ -f "${DESKTOP_SIG_GEN}" ]; then
                    DESKTOP_SIG_SRC="${DESKTOP_SIG_GEN}"
                fi
            fi
        fi

        if [ "$PRODUCTION" = true ]; then
            if [ -z "${DESKTOP_SIG_SRC}" ]; then
                echo -e "${RED}Production build requires DESKTOP.SIG for DESKTOP.CML, but none was found or generated.${NC}" >&2
                if ! command -v openssl >/dev/null 2>&1; then
                    echo -e "${RED}Hint: install openssl to generate DESKTOP.SIG automatically.${NC}" >&2
                else
                    echo -e "${RED}Hint: provide ${PROJECT_DIR}/DESKTOP.SIG or ${PROJECT_DIR}/desktop.sig, or create ${PROJECT_DIR}/keys/bootgate_rsa_priv.pem.${NC}" >&2
                fi
                exit 1
            fi

            DESKTOP_SIG_SIZE=$(wc -c < "${DESKTOP_SIG_SRC}" 2>/dev/null || echo 0)
            if [ "${DESKTOP_SIG_SIZE}" != "256" ]; then
                echo -e "${RED}Production build: DESKTOP.SIG must be exactly 256 bytes (RSA-2048). Got ${DESKTOP_SIG_SIZE}.${NC}" >&2
                exit 1
            fi
        fi

        if [ -n "${DESKTOP_SIG_SRC}" ]; then
            mcopy -o -i "${RAMDISK_TEMP}" "${DESKTOP_SIG_SRC}" ::/DESKTOP.SIG >/dev/null 2>&1
        fi
    fi

    # Optional signature file for /SYSTEM/UI/DESKTOP.CML (preferred trusted path).
    SYSUI_DESKTOP_SRC=""
    if [ -f "${RAMDISK_DIR}/system/ui/desktop.cml" ]; then
        SYSUI_DESKTOP_SRC="${RAMDISK_DIR}/system/ui/desktop.cml"
    elif [ -f "${RAMDISK_DIR}/system/ui/DESKTOP.CML" ]; then
        SYSUI_DESKTOP_SRC="${RAMDISK_DIR}/system/ui/DESKTOP.CML"
    fi

    if [ -n "${SYSUI_DESKTOP_SRC}" ]; then
        SYSUI_SIG_SRC=""
        if [ -f "${RAMDISK_DIR}/system/ui/DESKTOP.SIG" ]; then
            SYSUI_SIG_SRC="${RAMDISK_DIR}/system/ui/DESKTOP.SIG"
        elif [ -f "${RAMDISK_DIR}/system/ui/desktop.sig" ]; then
            SYSUI_SIG_SRC="${RAMDISK_DIR}/system/ui/desktop.sig"
        else
            BOOTGATE_PRIV_KEY=""
            if [ -f "${PROJECT_DIR}/keys/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${PROJECT_DIR}/keys/bootgate_rsa_priv.pem"
            elif [ -f "${BUILD_DIR}/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${BUILD_DIR}/bootgate_rsa_priv.pem"
            fi

            if command -v openssl >/dev/null 2>&1 && [ -n "${BOOTGATE_PRIV_KEY}" ]; then
                SYSUI_SIG_GEN="${BUILD_DIR}/SYSUIDES.SIG"
                sign_sha256_rsa2048_no_prompt "${BOOTGATE_PRIV_KEY}" "${SYSUI_DESKTOP_SRC}" "${SYSUI_SIG_GEN}" >/dev/null 2>&1 || true
                if [ -f "${SYSUI_SIG_GEN}" ]; then
                    SYSUI_SIG_SRC="${SYSUI_SIG_GEN}"
                fi
            fi
        fi

        if [ "$PRODUCTION" = true ]; then
            if [ -z "${SYSUI_SIG_SRC}" ]; then
                echo -e "${RED}Production build requires DESKTOP.SIG for /SYSTEM/UI/DESKTOP.CML, but none was found or generated.${NC}" >&2
                if ! command -v openssl >/dev/null 2>&1; then
                    echo -e "${RED}Hint: install openssl to generate DESKTOP.SIG automatically.${NC}" >&2
                else
                    echo -e "${RED}Hint: create ${RAMDISK_DIR}/system/ui/DESKTOP.SIG or provide ${PROJECT_DIR}/keys/bootgate_rsa_priv.pem.${NC}" >&2
                fi
                exit 1
            fi

            SYSUI_SIG_SIZE=$(wc -c < "${SYSUI_SIG_SRC}" 2>/dev/null || echo 0)
            if [ "${SYSUI_SIG_SIZE}" != "256" ]; then
                echo -e "${RED}Production build: /SYSTEM/UI/DESKTOP.SIG must be exactly 256 bytes (RSA-2048). Got ${SYSUI_SIG_SIZE}.${NC}" >&2
                exit 1
            fi
        fi

        if [ -n "${SYSUI_SIG_SRC}" ]; then
            mcopy -o -i "${RAMDISK_TEMP}" "${SYSUI_SIG_SRC}" ::/SYSTEM/UI/DESKTOP.SIG >/dev/null 2>&1
        fi
    fi

    # Optional signatures for CUIMLSS styles under /SYSTEM/UI.
    # In production mode, any .cxs present under ramdisk/system/ui must have a matching .sig
    # (or we must be able to generate one using keys/bootgate_rsa_priv.pem).
    if [ -d "${RAMDISK_DIR}/system/ui" ]; then
        for CXS_SRC in "${RAMDISK_DIR}/system/ui/"*.cxs "${RAMDISK_DIR}/system/ui/"*.CXS; do
            if [ ! -f "${CXS_SRC}" ]; then
                continue
            fi

            CXS_BASE=$(basename "${CXS_SRC}")
            CXS_NAME="${CXS_BASE%.*}"
            CXS_UPPER=$(echo "${CXS_NAME}" | tr '[:lower:]' '[:upper:]')

            CXS_SIG_SRC=""
            if [ -f "${CXS_SRC%.*}.SIG" ]; then
                CXS_SIG_SRC="${CXS_SRC%.*}.SIG"
            elif [ -f "${CXS_SRC%.*}.sig" ]; then
                CXS_SIG_SRC="${CXS_SRC%.*}.sig"
            fi

            if [ -z "${CXS_SIG_SRC}" ]; then
                BOOTGATE_PRIV_KEY=""
                if [ -f "${PROJECT_DIR}/keys/bootgate_rsa_priv.pem" ]; then
                    BOOTGATE_PRIV_KEY="${PROJECT_DIR}/keys/bootgate_rsa_priv.pem"
                elif [ -f "${BUILD_DIR}/bootgate_rsa_priv.pem" ]; then
                    BOOTGATE_PRIV_KEY="${BUILD_DIR}/bootgate_rsa_priv.pem"
                fi

                if command -v openssl >/dev/null 2>&1 && [ -n "${BOOTGATE_PRIV_KEY}" ]; then
                    CXS_SIG_GEN="${BUILD_DIR}/CXS_${CXS_UPPER}.SIG"
                    sign_sha256_rsa2048_no_prompt "${BOOTGATE_PRIV_KEY}" "${CXS_SRC}" "${CXS_SIG_GEN}" >/dev/null 2>&1 || true
                    if [ -f "${CXS_SIG_GEN}" ]; then
                        CXS_SIG_SRC="${CXS_SIG_GEN}"
                    fi
                fi
            fi

            if [ "$PRODUCTION" = true ]; then
                if [ -z "${CXS_SIG_SRC}" ]; then
                    echo -e "${RED}Production build requires a signature for CUIMLSS file ${CXS_BASE}, but none was found or generated.${NC}" >&2
                    if ! command -v openssl >/dev/null 2>&1; then
                        echo -e "${RED}Hint: install openssl to generate CUIMLSS signatures automatically.${NC}" >&2
                    else
                        echo -e "${RED}Hint: provide ${CXS_SRC%.*}.sig or create ${PROJECT_DIR}/keys/bootgate_rsa_priv.pem.${NC}" >&2
                    fi
                    exit 1
                fi

                CXS_SIG_SIZE=$(wc -c < "${CXS_SIG_SRC}" 2>/dev/null || echo 0)
                if [ "${CXS_SIG_SIZE}" != "256" ]; then
                    echo -e "${RED}Production build: CUIMLSS signature for ${CXS_BASE} must be exactly 256 bytes (RSA-2048). Got ${CXS_SIG_SIZE}.${NC}" >&2
                    exit 1
                fi
            fi

            if [ -n "${CXS_SIG_SRC}" ]; then
                mcopy -o -i "${RAMDISK_TEMP}" "${CXS_SIG_SRC}" "::/SYSTEM/UI/${CXS_UPPER}.SIG" >/dev/null 2>&1
            fi
        done
    fi

    # Optional production overrides for desktop presentation.
    # Stored as DESKOVR.JSN (8.3) for early FAT32 reader compatibility.
    if [ -f "${PROJECT_DIR}/desktop-overrides.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop-overrides.json" ::/DESKOVR.JSN >/dev/null 2>&1
    fi

    # Optional early-boot manifests / posture config.
    # These are stored as fixed 8.3 names so the early FAT32 reader can open them.
    if [ -f "${PROJECT_DIR}/security.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/security.json" ::/SECURITY.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/services.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/services.json" ::/SERVICES.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/drivers.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/drivers.json" ::/DRIVERS.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/apps.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/apps.json" ::/APPS.JSN >/dev/null 2>&1
    fi

    # Two-tier config trees (8.3-only; no LFN). These enable Step 9's production vs golden selection.
    # NOTE: today both tiers are populated identically; later, installers/updaters can diverge PROD.
    mdir -i "${RAMDISK_TEMP}" ::/PROD >/dev/null 2>&1 || mmd -i "${RAMDISK_TEMP}" ::/PROD >/dev/null 2>&1 || true
    mdir -i "${RAMDISK_TEMP}" ::/GOLDEN >/dev/null 2>&1 || mmd -i "${RAMDISK_TEMP}" ::/GOLDEN >/dev/null 2>&1 || true

    if [ -f "${PROJECT_DIR}/desktop.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop.json" ::/PROD/DESKTOP.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop.json" ::/GOLDEN/DESKTOP.JSN >/dev/null 2>&1
    fi

    if [ -n "${CUI_DESKTOP_SRC}" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${CUI_DESKTOP_SRC}" ::/PROD/DESKTOP.CML >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${CUI_DESKTOP_SRC}" ::/GOLDEN/DESKTOP.CML >/dev/null 2>&1
    fi

    # Negative test hook: intentionally corrupt only the production tier desktop JSON so
    # boot-time tier selection must fall back to GOLDEN.
    # Usage: CITADEL_NEGTEST_PROD_DESKTOP=1 ./build.sh -r
    if [ "${CITADEL_NEGTEST_PROD_DESKTOP}" = "1" ]; then
        echo -e "${YELLOW}      NEGTEST: corrupting /PROD/DESKTOP.JSN to force GOLDEN fallback...${NC}"
        NEGTEST_BAD_DESKTOP="${BUILD_DIR}/NEGTEST_DESKTOP_BAD.JSN"
        printf '{"desktop":{}}' > "${NEGTEST_BAD_DESKTOP}" 2>/dev/null || true
        mcopy -i "${RAMDISK_TEMP}" "${NEGTEST_BAD_DESKTOP}" ::/PROD/DESKTOP.JSN >/dev/null 2>&1 || true
    fi

    # Optional seasonal desktop presets (theme-only switching). Stored as fixed 8.3 names.
    # These are consumed by QDesktop when desktop-overrides.json includes: { "season": "spring" } (etc).
    if [ -f "${PROJECT_DIR}/desktopspring.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopspring.json" ::/DSPRING.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopspring.json" ::/PROD/DSPRING.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopspring.json" ::/GOLDEN/DSPRING.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/desktopsummer.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopsummer.json" ::/DSUMMER.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopsummer.json" ::/PROD/DSUMMER.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopsummer.json" ::/GOLDEN/DSUMMER.JSN >/dev/null 2>&1
    fi

    # Note: file is named desktopAutum.json in-repo (typo kept for compatibility).
    if [ -f "${PROJECT_DIR}/desktopAutum.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopAutum.json" ::/DAUTUMN.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopAutum.json" ::/PROD/DAUTUMN.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopAutum.json" ::/GOLDEN/DAUTUMN.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/desktopWinter.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopWinter.json" ::/DWINTER.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopWinter.json" ::/PROD/DWINTER.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktopWinter.json" ::/GOLDEN/DWINTER.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/desktop-overrides.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop-overrides.json" ::/PROD/DESKOVR.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/desktop-overrides.json" ::/GOLDEN/DESKOVR.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/security.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/security.json" ::/PROD/SECURITY.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/security.json" ::/GOLDEN/SECURITY.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/services.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/services.json" ::/PROD/SERVICES.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/services.json" ::/GOLDEN/SERVICES.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/drivers.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/drivers.json" ::/PROD/DRIVERS.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/drivers.json" ::/GOLDEN/DRIVERS.JSN >/dev/null 2>&1
    fi

    if [ -f "${PROJECT_DIR}/apps.json" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/apps.json" ::/PROD/APPS.JSN >/dev/null 2>&1
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/apps.json" ::/GOLDEN/APPS.JSN >/dev/null 2>&1
    fi

    # Include startup.cfg so kernel startup-mode parsing can work from the ramdisk
    if [ -f "${PROJECT_DIR}/startup.cfg" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${PROJECT_DIR}/startup.cfg" ::/STARTUP.CFG >/dev/null 2>&1
    fi

    # Include boot.json (boot policy / min spec gate) as an 8.3 name.
    # Prefer project-root boot.json, but also allow a checked-in copy under kernel/Boot/Config/.
    BOOT_JSON_SRC=""
    if [ -f "${PROJECT_DIR}/boot.json" ]; then
        BOOT_JSON_SRC="${PROJECT_DIR}/boot.json"
    elif [ -f "${PROJECT_DIR}/kernel/Boot/Config/boot.json" ]; then
        BOOT_JSON_SRC="${PROJECT_DIR}/kernel/Boot/Config/boot.json"
    fi

    if [ -n "${BOOT_JSON_SRC}" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${BOOT_JSON_SRC}" ::/BOOT.JSN >/dev/null 2>&1

        # Optional signature file for BOOT.JSN.
        # If you provide a raw BOOT.SIG (RSA-2048 signature, 256 bytes) it will be packed as 8.3.
        # If you provide a signing key at build/bootgate_rsa_priv.pem and have openssl installed,
        # we will generate BOOT.SIG automatically.
        BOOT_SIG_SRC=""
        if [ -f "${PROJECT_DIR}/BOOT.SIG" ]; then
            BOOT_SIG_SRC="${PROJECT_DIR}/BOOT.SIG"
        elif [ -f "${PROJECT_DIR}/boot.sig" ]; then
            BOOT_SIG_SRC="${PROJECT_DIR}/boot.sig"
        else
            BOOTGATE_PRIV_KEY=""
            if [ -f "${PROJECT_DIR}/keys/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${PROJECT_DIR}/keys/bootgate_rsa_priv.pem"
            elif [ -f "${BUILD_DIR}/bootgate_rsa_priv.pem" ]; then
                # Legacy location (wiped by --clean); prefer keys/.
                BOOTGATE_PRIV_KEY="${BUILD_DIR}/bootgate_rsa_priv.pem"
            fi

            if command -v openssl >/dev/null 2>&1 && [ -n "${BOOTGATE_PRIV_KEY}" ]; then
                BOOT_SIG_GEN="${BUILD_DIR}/BOOT.SIG"
                sign_sha256_rsa2048_no_prompt "${BOOTGATE_PRIV_KEY}" "${BOOT_JSON_SRC}" "${BOOT_SIG_GEN}" >/dev/null 2>&1 || true
                if [ -f "${BOOT_SIG_GEN}" ]; then
                    BOOT_SIG_SRC="${BOOT_SIG_GEN}"
                fi
            fi
        fi

        if [ "$PRODUCTION" = true ]; then
            if [ -z "${BOOT_SIG_SRC}" ]; then
                echo -e "${RED}Production build requires BOOT.SIG for BOOT.JSN, but none was found or generated.${NC}" >&2
                if ! command -v openssl >/dev/null 2>&1; then
                    echo -e "${RED}Hint: install openssl to generate BOOT.SIG automatically.${NC}" >&2
                else
                    echo -e "${RED}Hint: provide ${PROJECT_DIR}/BOOT.SIG or ${PROJECT_DIR}/boot.sig, or create ${PROJECT_DIR}/keys/bootgate_rsa_priv.pem.${NC}" >&2
                fi
                exit 1
            fi

            BOOT_SIG_SIZE=$(wc -c < "${BOOT_SIG_SRC}" 2>/dev/null || echo 0)
            if [ "${BOOT_SIG_SIZE}" != "256" ]; then
                echo -e "${RED}Production build: BOOT.SIG must be exactly 256 bytes (RSA-2048). Got ${BOOT_SIG_SIZE}.${NC}" >&2
                exit 1
            fi
        fi

        if [ -n "${BOOT_SIG_SRC}" ]; then
            mcopy -i "${RAMDISK_TEMP}" "${BOOT_SIG_SRC}" ::/BOOT.SIG >/dev/null 2>&1
        fi
    fi

    # Include sysconfig.json (root config index) as an 8.3 name.
    # Prefer project-root sysconfig.json.
    # Stored as SYSCFG.JSN because our early FAT32 reader does not support LFN yet.
    SYSCONFIG_JSON_SRC=""
    if [ -f "${PROJECT_DIR}/sysconfig.json" ]; then
        SYSCONFIG_JSON_SRC="${PROJECT_DIR}/sysconfig.json"
    fi

    if [ -n "${SYSCONFIG_JSON_SRC}" ]; then
        mcopy -i "${RAMDISK_TEMP}" "${SYSCONFIG_JSON_SRC}" ::/SYSCFG.JSN >/dev/null 2>&1

        # Optional signature file for SYSCFG.JSN.
        SYSCFG_SIG_SRC=""
        if [ -f "${PROJECT_DIR}/SYSCFG.SIG" ]; then
            SYSCFG_SIG_SRC="${PROJECT_DIR}/SYSCFG.SIG"
        elif [ -f "${PROJECT_DIR}/syscfg.sig" ]; then
            SYSCFG_SIG_SRC="${PROJECT_DIR}/syscfg.sig"
        elif [ -f "${PROJECT_DIR}/sysconfig.sig" ]; then
            SYSCFG_SIG_SRC="${PROJECT_DIR}/sysconfig.sig"
        else
            BOOTGATE_PRIV_KEY=""
            if [ -f "${PROJECT_DIR}/keys/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${PROJECT_DIR}/keys/bootgate_rsa_priv.pem"
            elif [ -f "${BUILD_DIR}/bootgate_rsa_priv.pem" ]; then
                BOOTGATE_PRIV_KEY="${BUILD_DIR}/bootgate_rsa_priv.pem"
            fi

            if command -v openssl >/dev/null 2>&1 && [ -n "${BOOTGATE_PRIV_KEY}" ]; then
                SYSCFG_SIG_GEN="${BUILD_DIR}/SYSCFG.SIG"
                sign_sha256_rsa2048_no_prompt "${BOOTGATE_PRIV_KEY}" "${SYSCONFIG_JSON_SRC}" "${SYSCFG_SIG_GEN}" >/dev/null 2>&1 || true
                if [ -f "${SYSCFG_SIG_GEN}" ]; then
                    SYSCFG_SIG_SRC="${SYSCFG_SIG_GEN}"
                fi
            fi
        fi

        if [ "$PRODUCTION" = true ]; then
            if [ -z "${SYSCFG_SIG_SRC}" ]; then
                echo -e "${RED}Production build requires SYSCFG.SIG for SYSCFG.JSN, but none was found or generated.${NC}" >&2
                if ! command -v openssl >/dev/null 2>&1; then
                    echo -e "${RED}Hint: install openssl to generate SYSCFG.SIG automatically.${NC}" >&2
                else
                    echo -e "${RED}Hint: provide ${PROJECT_DIR}/SYSCFG.SIG (8.3) or ${PROJECT_DIR}/sysconfig.sig, or create ${PROJECT_DIR}/keys/bootgate_rsa_priv.pem.${NC}" >&2
                fi
                exit 1
            fi

            SYSCFG_SIG_SIZE=$(wc -c < "${SYSCFG_SIG_SRC}" 2>/dev/null || echo 0)
            if [ "${SYSCFG_SIG_SIZE}" != "256" ]; then
                echo -e "${RED}Production build: SYSCFG.SIG must be exactly 256 bytes (RSA-2048). Got ${SYSCFG_SIG_SIZE}.${NC}" >&2
                exit 1
            fi
        fi

        if [ -n "${SYSCFG_SIG_SRC}" ]; then
            mcopy -i "${RAMDISK_TEMP}" "${SYSCFG_SIG_SRC}" ::/SYSCFG.SIG >/dev/null 2>&1
        fi
    fi

    if [ "$PRODUCTION" = true ] && [ -z "${SYSCONFIG_JSON_SRC}" ]; then
        echo -e "${RED}Production build requires sysconfig.json to package as SYSCFG.JSN.${NC}" >&2
        echo -e "${RED}Hint: create ${PROJECT_DIR}/sysconfig.json.${NC}" >&2
        exit 1
    fi

    if [ "$PRODUCTION" = true ] && [ -z "${BOOT_JSON_SRC}" ]; then
        echo -e "${RED}Production build requires a boot policy file (boot.json) to package as BOOT.JSN.${NC}" >&2
        echo -e "${RED}Hint: create ${PROJECT_DIR}/boot.json (preferred) or ${PROJECT_DIR}/kernel/Boot/Config/boot.json.${NC}" >&2
        exit 1
    fi
    cp "${RAMDISK_TEMP}" "${BUILD_DIR}/ramdisk.img"
    cp "${RAMDISK_TEMP}" "${RAMDISK_OUTPUT}"
    echo -e "${GREEN}      Ramdisk written to modules/ramdisk.img${NC}"
    echo -e "${GREEN}      Module bundle: ${MODULE_RAMDISK_IMAGE}${NC}"
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

# UEFI removable media path. Some firmware/tools expect BOOTX64.EFI to exist in the ISO filesystem
# in addition to (or instead of) an El Torito EFI boot image.
mkdir -p "${ISO_DIR}/EFI/BOOT"
if [ ! -f "${ISO_DIR}/EFI/BOOT/BOOTX64.EFI" ]; then
    if [ -f "${LIMINE_DIR}/BOOTX64.EFI" ]; then
        cp "${LIMINE_DIR}/BOOTX64.EFI" "${ISO_DIR}/EFI/BOOT/BOOTX64.EFI"
    else
        echo -e "${RED}Missing Limine UEFI boot file: BOOTX64.EFI${NC}"
        echo -e "${RED}Hint: run 'git submodule update --init --recursive' to populate limine.${NC}"
        exit 1
    fi
fi

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
    -eltorito-alt-boot \
    -e boot/limine/limine-uefi-cd.bin \
    -no-emul-boot \
    --protective-msdos-label \
    -isohybrid-gpt-basdat \
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
echo -e "  Kernel artifact: ${KERNEL_ARTIFACT_ELF}"
if [ -f "${KERNEL_ARTIFACT_BIN}" ]; then
    echo -e "  Kernel binary:   ${KERNEL_ARTIFACT_BIN}"
fi
if [ -f "${MODULE_RAMDISK_IMAGE}" ]; then
    echo -e "  Modules:         ${MODULE_BUNDLE_DIR}"
    echo -e "    ramdisk:       ${MODULE_RAMDISK_IMAGE}"
fi
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

    if [ "$PRODUCTION" = true ] && [ "$TPM" = false ]; then
        echo -e "${YELLOW}Warning: --prod without --tpm will likely refuse to boot (TPM required for production enforcement).${NC}"
    fi

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
        SWTPM_LOG="${SWTPM_DIR}/swtpm.log"
        mkdir -p "${SWTPM_DIR}" "${SWTPM_DIR}/state"
        rm -f "${SWTPM_SOCK}"

        echo -e "${GREEN}Starting swtpm (TPM2) at ${SWTPM_SOCK}${NC}"
        swtpm socket --tpm2 \
            --tpmstate dir="${SWTPM_DIR}/state" \
            --ctrl type=unixio,path="${SWTPM_SOCK}" \
            --log level=20 \
            >"${SWTPM_LOG}" 2>&1 &
        SWTPM_PID=$!

        cleanup_swtpm() {
            if [ -n "${SWTPM_PID}" ] && kill -0 "${SWTPM_PID}" 2>/dev/null; then
                kill "${SWTPM_PID}" 2>/dev/null || true
                wait "${SWTPM_PID}" 2>/dev/null || true
            fi
            rm -f "${SWTPM_SOCK}" 2>/dev/null || true
        }
        trap cleanup_swtpm EXIT

        # Wait for swtpm to create the socket before launching QEMU.
        # Without this, QEMU can race and fail with "No such file".
        SWTPM_READY=false
        for _ in $(seq 1 100); do
            if [ -S "${SWTPM_SOCK}" ]; then
                SWTPM_READY=true
                break
            fi
            if ! kill -0 "${SWTPM_PID}" 2>/dev/null; then
                echo -e "${RED}swtpm exited before creating its socket.${NC}" >&2
                echo -e "${RED}See log: ${SWTPM_LOG}${NC}" >&2
                tail -50 "${SWTPM_LOG}" 2>/dev/null || true
                exit 1
            fi
            sleep 0.05
        done
        if [ "${SWTPM_READY}" != "true" ]; then
            echo -e "${RED}swtpm socket did not appear in time: ${SWTPM_SOCK}${NC}" >&2
            echo -e "${RED}See log: ${SWTPM_LOG}${NC}" >&2
            tail -50 "${SWTPM_LOG}" 2>/dev/null || true
            exit 1
        fi

        TPM_ARGS=(
            -machine pc
            -chardev "socket,id=chrtpm,path=${SWTPM_SOCK}"
            -tpmdev "emulator,id=tpm0,chardev=chrtpm"
            -device "tpm-crb,tpmdev=tpm0"
        )
    fi

    SHARED_ARGS=()
    if [ -d "${SHARED_DIR}" ]; then
        echo -e "${GREEN}Mounting shared folder at ${SHARED_DIR}${NC}"
        if [ "$TPM" = true ]; then
            # In q35+TPM mode, explicitly attach the host share as a secondary IDE disk
            # on the primary channel (unit=1).
            SHARED_ARGS=(
                -drive "id=shareddisk,if=none,file=fat:rw:${SHARED_DIR},format=raw"
                -device "ide-hd,drive=shareddisk,bus=ide.0,unit=1"
            )
        else
            SHARED_ARGS=(-drive "file=fat:rw:${SHARED_DIR},format=raw,if=ide,index=1")
        fi
    else
        echo -e "${YELLOW}Shared folder not found at ${SHARED_DIR}; skipping host share (mkdir shared to enable).${NC}"
    fi

    SYSTEM_ARGS=()
    if [ "$SYSTEM_VOL" = true ]; then
        SYSTEM_DISK="${BUILD_DIR}/system.qcow2"
        if ! command -v qemu-img &> /dev/null; then
            echo -e "${RED}qemu-img not found (needed to create ${SYSTEM_DISK}). Install qemu-utils.${NC}"
            exit 1
        fi
        if [ ! -f "${SYSTEM_DISK}" ]; then
            qemu-img create -f qcow2 "${SYSTEM_DISK}" 256M >/dev/null
            echo -e "${GREEN}Created system volume: ${SYSTEM_DISK}${NC}"
            echo -e "${YELLOW}Note: format it as FAT in-guest before it can mount as /system.${NC}"
        fi
        if [ "$TPM" = true ]; then
            # In q35+TPM mode, explicitly attach the system disk as primary IDE master (unit=0).
            SYSTEM_ARGS=(
                -drive "id=systemdisk,if=none,file=${SYSTEM_DISK},media=disk,format=qcow2"
                -device "ide-hd,drive=systemdisk,bus=ide.0,unit=0"
            )
        else
            SYSTEM_ARGS=( -drive "file=${SYSTEM_DISK},if=ide,index=0,media=disk,format=qcow2" )
        fi
    fi

    # Use xHCI controller with a USB tablet (absolute) by default.
    # This avoids QEMU relative-mouse grab artifacts and improves 1:1 UI hit-testing.
    INPUT_DEVICE=( -device usb-tablet,bus=xhci.0 )
    if [ "$USE_TABLET" = false ]; then
        INPUT_DEVICE=( -device usb-mouse,bus=xhci.0  -device usb-kbd,bus=xhci.0 )
    fi

    QEMU_ARGS=(
        -cdrom "${ISO_FILE}"
        -boot order=d
        -m 3G
        -vga vmware
        -netdev user,id=net0
        -device e1000,netdev=net0
        # Allow the guest to terminate QEMU cleanly via I/O port 0xF4.
        -device isa-debug-exit,iobase=0xf4,iosize=0x04
        -device qemu-xhci,id=xhci
        "${INPUT_DEVICE[@]}"
        -serial file:"${SERIAL_LOG}"
    )

    if [ "$HEADLESS" = true ]; then
        QEMU_ARGS+=( -display none )
    fi
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

    # Best-effort: avoid the common "first click focuses QEMU but doesn't reach the guest" behavior
    # on some host window managers by grabbing input when the mouse enters the QEMU window.
    # Only add this when QEMU reports both a supported display backend AND that the backend
    # supports the specific suboption (QEMU builds vary).
    if [ "$HEADLESS" = false ] && [ "$AUTO_GRAB" = true ]; then
        DISPLAY_HELP="$(${QEMU_BIN} -display help 2>/dev/null || true)"
        HELP_TEXT="$(${QEMU_BIN} -help 2>/dev/null || true)"

        if echo "${DISPLAY_HELP}" | grep -q "gtk"; then
            if echo "${HELP_TEXT}" | grep -i "^-display gtk" | grep -q "grab-on-hover"; then
                QEMU_ARGS+=( -display gtk,grab-on-hover=on )
            fi
        elif echo "${DISPLAY_HELP}" | grep -q "sdl"; then
            if echo "${HELP_TEXT}" | grep -i "^-display sdl" | grep -q "grab-on-hover"; then
                QEMU_ARGS+=( -display sdl,grab-on-hover=on )
            fi
        fi
    fi

    DISK_ARGS=( "${SYSTEM_ARGS[@]}" "${SHARED_ARGS[@]}" )
    #QEMU_CMD = "qemu-system-x86_64 "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${DISK_ARGS[@]}"  
    echo "${QEMU_BIN} ${QEMU_ARGS[@]} ${TPM_ARGS[@]} ${DISK_ARGS[@]}"
    if [ "${RUN_FOR_SECONDS}" -gt 0 ]; then
        if command -v timeout >/dev/null 2>&1; then
            echo -e "${YELLOW}Auto-stopping QEMU after ${RUN_FOR_SECONDS}s (use Ctrl+Q in guest for clean exit).${NC}"
            timeout --preserve-status "${RUN_FOR_SECONDS}" "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${DISK_ARGS[@]}" || true
        else
            echo -e "${YELLOW}timeout(1) not found; running QEMU normally.${NC}"
            "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${DISK_ARGS[@]}"
        fi
    else
        "${QEMU_BIN}" "${QEMU_ARGS[@]}" "${TPM_ARGS[@]}" "${DISK_ARGS[@]}"
    fi
    echo ""
    echo -e "${CYAN}=== Serial output (last 30 lines) ===${NC}"
    tail -30 "${SERIAL_LOG}"
else
    echo -e "${YELLOW}Note: QEMU was not launched (use -r/--run to run after building).${NC}"
    echo -e "${YELLOW}Example: ./build.sh -r${NC}"
fi
