#!/usr/bin/env python3
"""Run the Citadel in-guest installer flow in a disposable QEMU disk."""

from __future__ import annotations

import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import smp_qemu_smoke as smoke


PROJECT_DIR = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_DIR / "build"
DEFAULT_ISO = BUILD_DIR / "citadel-limine.iso"
SYSTEM_DISK = BUILD_DIR / "system.qcow2"


def reset_system_disk() -> None:
    qemu_img = shutil.which("qemu-img")
    if not qemu_img:
        raise FileNotFoundError("qemu-img is required for the installer smoke disk")
    result = subprocess.run(
        [qemu_img, "create", "-f", "qcow2", str(SYSTEM_DISK), "256M"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())


def wait_for_log(serial_path: Path, needles: tuple[str, ...], timeout: float) -> str:
    deadline = time.monotonic() + timeout
    cursor = 0
    while time.monotonic() < deadline:
        if serial_path.is_file():
            text = serial_path.read_text(encoding="utf-8", errors="replace")
            chunk = text[cursor:]
            cursor = len(text)
            for needle in needles:
                if needle in chunk or needle in text:
                    return needle
        time.sleep(0.1)
    raise TimeoutError(f"timed out waiting for installer markers: {needles}")


def wait_for_occurrences(
    serial_path: Path, needle: str, minimum: int, timeout: float
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if serial_path.is_file():
            text = serial_path.read_text(encoding="utf-8", errors="replace")
            if text.count(needle) >= minimum:
                return
        time.sleep(0.1)
    raise TimeoutError(
        f"timed out waiting for {needle!r} to occur {minimum} time(s)"
    )


def type_slow(client: smoke.QmpClient, command: str) -> None:
    for character in command:
        client.press(character)
        time.sleep(0.15)
    client.press_enter()


def run_case(args: argparse.Namespace) -> None:
    if not args.iso.is_file():
        raise FileNotFoundError(f"ISO not found: {args.iso}")
    if not smoke.SHARED_DIR.is_dir():
        raise FileNotFoundError(f"shared directory not found: {smoke.SHARED_DIR}")

    serial_path = BUILD_DIR / "installer-qemu.serial.log"
    serial_path.unlink(missing_ok=True)

    with tempfile.TemporaryDirectory(prefix="citadel-installer-qmp-") as temp_dir:
        qmp_path = Path(temp_dir) / "qmp.sock"
        tpm_process: subprocess.Popen | None = None
        tpm_socket: Path | None = None
        process: subprocess.Popen | None = None
        client: smoke.QmpClient | None = None
        try:
            if args.tpm:
                tpm_process, tpm_socket = smoke.start_swtpm(os.getpid(), 2)
            command = smoke.qemu_arguments(
                smoke.qemu_binary(args.qemu),
                args.iso.resolve(),
                2,
                qmp_path,
                serial_path,
                False,
                tpm_socket,
                True,
            )
            process = subprocess.Popen(command)
            client = smoke.connect_qmp(qmp_path, process, timeout=30)
            smoke.select_startup_entry(client, "diagnostic")
            wait_for_log(
                serial_path,
                ("Entering console-only startup path", "=== TERMINAL MODE ==="),
                args.startup_delay,
            )
            time.sleep(5)
            type_slow(client, "clear")
            time.sleep(3)

            type_slow(client, "sysdisks")
            time.sleep(3)
            wait_for_log(
                serial_path,
                ("installer: stage=discover status=complete", "sysdisks: no AHCI/IDE disks detected"),
                args.command_timeout,
            )

            type_slow(client, "admin enable")
            time.sleep(2)
            type_slow(client, "admin enable")
            time.sleep(2)
            type_slow(client, "sysformat disk0 confirm")
            time.sleep(2)
            client.press_enter()
            wait_for_log(
                serial_path,
                ("installer: stage=complete status=success", "installer: status=failure"),
                args.command_timeout,
            )

            serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
            if "installer: stage=complete status=success" not in serial_text:
                raise RuntimeError("sysformat did not complete successfully")
            if "mount=/system" not in serial_text:
                raise RuntimeError("sysformat success did not confirm /system")

            type_slow(client, "sysverify")
            wait_for_log(
                serial_path,
                ("installer: stage=verify_payload status=success", "payload verification failed"),
                args.command_timeout,
            )

            serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
            if "installer: stage=verify_payload status=success" not in serial_text:
                raise RuntimeError("sysverify did not pass")

            mount_count = serial_text.count("[INFO ] QFS: Mounted filesystem at /system")
            verify_count = serial_text.count("installer: stage=verify_payload status=success")
            type_slow(client, "reboot now")
            wait_for_occurrences(
                serial_path,
                "[INFO ] QFS: Mounted filesystem at /system",
                mount_count + 1,
                args.command_timeout,
            )
            wait_for_log(
                serial_path,
                ("Entering console-only startup path", "=== TERMINAL MODE ==="),
                args.startup_delay,
            )
            time.sleep(5)
            type_slow(client, "clear")
            time.sleep(3)
            type_slow(client, "sysverify")
            wait_for_log(
                serial_path,
                ("installer: stage=verify_payload status=success", "payload verification failed"),
                args.command_timeout,
            )
            serial_text = serial_path.read_text(encoding="utf-8", errors="replace")
            if serial_text.count("installer: stage=verify_payload status=success") < verify_count + 1:
                raise RuntimeError("sysverify failed after reboot")

            print("PASS: QEMU installer format, mount, copy, and verification")
        finally:
            if client is not None:
                try:
                    client.execute("quit")
                except (OSError, RuntimeError, socket.timeout):
                    pass
            if process is not None and process.poll() is None:
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
            if tpm_process is not None:
                if tpm_process.poll() is None:
                    tpm_process.terminate()
                    try:
                        tpm_process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        tpm_process.kill()
                        tpm_process.wait()
                if tpm_socket:
                    tpm_socket.unlink(missing_ok=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the Citadel in-guest installer smoke test in QEMU."
    )
    parser.add_argument("--iso", type=Path, default=DEFAULT_ISO)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--tpm", action="store_true")
    parser.add_argument("--startup-delay", type=float, default=45)
    parser.add_argument("--command-timeout", type=float, default=180)
    parser.add_argument(
        "--reset-system-volume",
        action="store_true",
        help="destructively recreate build/system.qcow2 for this smoke test",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.reset_system_volume:
        print("FAIL: pass --reset-system-volume to recreate the disposable test disk", file=sys.stderr)
        return 2
    try:
        reset_system_disk()
        run_case(args)
        return 0
    except (FileNotFoundError, OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
