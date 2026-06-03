#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISO_PATH="${PROJECT_DIR}/build/citadel-limine.iso"
DEVICE=""
SKIP_BUILD=false
ASSUME_YES=false
BLOCK_SIZE="4M"
WINDOWS_WRITER_SCRIPT="${PROJECT_DIR}/tools/write_bootable_usb_windows.ps1"

is_wsl() {
  grep -qiE '(microsoft|wsl)' /proc/version 2>/dev/null
}

windows_drive_letter_from_mount_path() {
  local path="$1"
  if [[ "$path" =~ ^/mnt/([A-Za-z])(/.*)?$ ]]; then
    printf '%s\n' "${BASH_REMATCH[1]}"
  fi
}

windows_disk_number_for_drive_letter() {
  local drive_letter="$1"

  command -v powershell.exe >/dev/null 2>&1 || return 1
  powershell.exe -NoProfile -Command "(Get-Partition -DriveLetter ${drive_letter} | Select-Object -ExpandProperty DiskNumber)" 2>/dev/null \
    | tr -d '\r[:space:]'
}

windows_path_from_wsl_path() {
  local path="$1"

  if command -v wslpath >/dev/null 2>&1; then
    wslpath -w "$path"
    return
  fi

  if [[ "$path" == /mnt/* ]]; then
    local drive_letter rest
    drive_letter="$(windows_drive_letter_from_mount_path "$path")"
    rest="${path#"/mnt/${drive_letter}"}"
    rest="${rest//\//\\}"
    printf '%s\n' "${drive_letter^}:${rest}"
    return
  fi

  printf '%s\n' "$path"
}

maybe_write_via_windows() {
  local path="$1"
  local drive_letter disk_number windows_iso_path

  if ! is_wsl || [[ "$path" != /mnt/* ]]; then
    return 1
  fi

  command -v powershell.exe >/dev/null 2>&1 || return 1
  [[ -f "$WINDOWS_WRITER_SCRIPT" ]] || return 1

  drive_letter="$(windows_drive_letter_from_mount_path "$path")"
  [[ -n "$drive_letter" ]] || return 1

  disk_number="$(windows_disk_number_for_drive_letter "$drive_letter" || true)"
  [[ -n "$disk_number" ]] || return 1

  windows_iso_path="$(windows_path_from_wsl_path "$ISO_PATH")"
  echo "WSL detected with mounted Windows drive $path; delegating raw write to Windows PhysicalDrive${disk_number}."
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(windows_path_from_wsl_path "$WINDOWS_WRITER_SCRIPT")" \
    -IsoPath "$windows_iso_path" \
    -DiskNumber "$disk_number" \
    -DriveLetter "$drive_letter" \
    -SkipBuild \
    $( [[ "$ASSUME_YES" == true ]] && printf '%s' '-Force' )
  exit $?
}

print_wsl_mount_guidance_and_exit() {
  local path="$1"
  local drive_letter disk_number

  drive_letter="$(windows_drive_letter_from_mount_path "$path")"
  disk_number=""
  if [[ -n "$drive_letter" ]]; then
    disk_number="$(windows_disk_number_for_drive_letter "$drive_letter" || true)"
  fi

  echo "$path is a mounted Windows filesystem, not a raw block device." >&2
  if [[ -n "$drive_letter" && -n "$disk_number" ]]; then
    cat >&2 <<EOF
Drive ${drive_letter}: is on Windows physical disk ${disk_number}.

To write the Citadel ISO from WSL, attach the raw disk into WSL first.

In an elevated Windows PowerShell:
  Set-Disk -Number ${disk_number} -IsOffline \$true
  wsl.exe --mount \\\\.\\PHYSICALDRIVE${disk_number} --bare

Back in WSL:
  lsblk
  tools/write_bootable_usb.sh --device /dev/sdX --skip-build

When finished, in elevated Windows PowerShell:
  wsl.exe --unmount \\\\.\\PHYSICALDRIVE${disk_number}
  Set-Disk -Number ${disk_number} -IsOffline \$false
EOF
  else
    cat >&2 <<EOF
To write the Citadel ISO from WSL, offline the USB disk in Windows and attach it with:
  wsl.exe --mount \\\\.\\PHYSICALDRIVEn --bare

Then rerun this script with the /dev/sdX device that appears in WSL.
EOF
  fi
  exit 1
}

usage() {
  cat <<EOF
Usage: tools/write_bootable_usb.sh [options]

Build Citadel's bootable ISO and write it to a USB disk.

Options:
  --device <path>   Raw disk device to erase and write (example: /dev/sdb)
  --skip-build      Reuse the existing build/citadel-limine.iso
  --list            List likely USB/removable disks and exit
  --yes             Skip the interactive confirmation prompt
  --bs <size>       dd block size (default: 4M)
  -h, --help        Show this help

Examples:
  tools/write_bootable_usb.sh --list
  tools/write_bootable_usb.sh --device /dev/sdb
  tools/write_bootable_usb.sh --device /dev/sdb --skip-build --yes
EOF
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing required command: $1" >&2
    exit 1
  }
}

device_summary() {
  local path="$1"
  lsblk -dn -o PATH,SIZE,TRAN,RM,HOTPLUG,VENDOR,MODEL "$path" 2>/dev/null | sed 's/^[[:space:]]*//'
}

list_candidates() {
  lsblk -dpnr -o PATH,TYPE,TRAN,RM,HOTPLUG,SIZE,VENDOR,MODEL | awk '
    $2 == "disk" && ($3 == "usb" || $4 == "1" || $5 == "1") {
      printf "%s\t%s\t%s\t%s\t%s\t", $1, $3, $4, $5, $6;
      for (i = 7; i <= NF; ++i) {
        printf "%s%s", $i, (i < NF ? " " : "\n");
      }
    }
  '
}

print_candidates() {
  local candidates
  candidates="$(list_candidates)"
  if [[ -z "$candidates" ]]; then
    echo "No likely USB/removable disk devices found."
    return 0
  fi

  echo "Likely USB/removable disks:"
  while IFS=$'\t' read -r path tran rm hotplug size identity; do
    printf '  %s  size=%s tran=%s rm=%s hotplug=%s  %s\n' "$path" "$size" "$tran" "$rm" "$hotplug" "$identity"
  done <<< "$candidates"
}

pick_default_device() {
  local candidates count
  candidates="$(list_candidates)"
  count="$(printf '%s\n' "$candidates" | sed '/^$/d' | wc -l)"
  if [[ "$count" -eq 1 ]]; then
    printf '%s\n' "$candidates" | cut -f1
  fi
}

validate_device() {
  local path="$1"
  local device_type

  [[ -n "$path" ]] || {
    echo "No target device selected." >&2
    exit 1
  }

  if is_wsl && [[ "$path" == /mnt/* ]]; then
    maybe_write_via_windows "$path" || print_wsl_mount_guidance_and_exit "$path"
  fi

  [[ -b "$path" ]] || {
    echo "Not a block device: $path" >&2
    exit 1
  }

  device_type="$(lsblk -dn -o TYPE "$path" 2>/dev/null | head -n1 | tr -d '[:space:]')"
  [[ "$device_type" == "disk" ]] || {
    echo "Refusing to write to non-disk device: $path" >&2
    exit 1
  }
}

unmount_partitions() {
  local path="$1"
  local part target

  while read -r part; do
    [[ -n "$part" ]] || continue
    while read -r target; do
      [[ -n "$target" ]] || continue
      echo "Unmounting $target"
      sudo umount "$target"
    done < <(findmnt -rn -S "$part" -o TARGET)
  done < <(lsblk -lnpo PATH,TYPE "$path" | awk '$2 == "part" { print $1 }')
}

confirm_write() {
  local path="$1"
  local response

  if [[ "$ASSUME_YES" == true ]]; then
    return 0
  fi

  echo "About to erase and overwrite:"
  echo "  $(device_summary "$path")"
  echo "  ISO: $ISO_PATH"
  printf 'Type the full device path to continue: '
  read -r response
  [[ "$response" == "$path" ]] || {
    echo "Confirmation mismatch. Aborting." >&2
    exit 1
  }
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)
      DEVICE="${2:-}"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=true
      shift
      ;;
    --list)
      require_command lsblk
      print_candidates
      exit 0
      ;;
    --yes)
      ASSUME_YES=true
      shift
      ;;
    --bs)
      BLOCK_SIZE="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

require_command awk
require_command dd
require_command findmnt
require_command lsblk
require_command sudo
require_command sync

if [[ "$SKIP_BUILD" == false ]]; then
  "${PROJECT_DIR}/build.sh"
fi

[[ -f "$ISO_PATH" ]] || {
  echo "Missing ISO: $ISO_PATH" >&2
  echo "Run ./build.sh first or omit --skip-build." >&2
  exit 1
}

if [[ -z "$DEVICE" ]]; then
  DEVICE="$(pick_default_device || true)"
fi

if [[ -z "$DEVICE" ]]; then
  print_candidates
  echo "Specify a target raw disk with --device /dev/sdX" >&2
  exit 1
fi

validate_device "$DEVICE"
confirm_write "$DEVICE"
unmount_partitions "$DEVICE"

echo "Writing $ISO_PATH to $DEVICE"
sudo dd if="$ISO_PATH" of="$DEVICE" bs="$BLOCK_SIZE" status=progress conv=fsync oflag=sync
sync
echo "Bootable USB write complete for $DEVICE"