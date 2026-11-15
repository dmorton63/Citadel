#!/usr/bin/env python3
"""Run and validate Citadel SMP work tests in QEMU."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_ISO = PROJECT_DIR / "build" / "citadel-limine.iso"
SHARED_DIR = PROJECT_DIR / "shared"
BUILD_DIR = PROJECT_DIR / "build"
SYSTEM_DISK = BUILD_DIR / "system.qcow2"
OVMF_DIR = Path(os.environ.get("OVMF_DIR", "/usr/share/OVMF"))
SECUREBOOT_VARS = BUILD_DIR / "secureboot" / "OVMF_VARS_citadel.fd"
# Plain OVMF reports Setup Mode (no PK) on q35, but no Secure Boot support on pc.
EXPECTED_SECURE_BOOT = {
    "bios": ("secure_boot: off (legacy BIOS)",),
    "uefi": ("secure_boot: off (firmware)", "secure_boot: off (setup mode)"),
    "secureboot": ("secure_boot: on (firmware)",),
}
WORK_ITEMS = 10_000
ITERATIONS = 10
U64_MASK = (1 << 64) - 1
CHECKSUM_MULTIPLIER = 6_364_136_223_846_793_005
CHECKSUM_INCREMENT = 1_442_695_040_888_963_407
SLOT_PATTERN = re.compile(
    r"^\s*\[\d+\] cpu=(\d+) lapic=\d+ state=(\w+) online=(\d+)"
    r".*?\bth=(\d+)\s+sh=(\d+)\s+work=(\d+)"
    r"\s+t0=(\d+)\s+t1=(\d+)\s+t2=(\d+)\s+w2=(\d+)\s+reaped=(\d+)\s*$",
    re.MULTILINE,
)
WORK_PATTERN = re.compile(
    r"^smpwork_requested: (\d+)\s*\n"
    r"smpwork_completed: (\d+)\s*\n"
    r"smpwork_checksum: (\d+)\s*\n"
    r"smpwork: complete\s*$",
    re.MULTILINE,
)
SELFTEST_PATTERN = re.compile(
    r"^smpselftest_aps: (\d+)\s*\n"
    r"smpselftest_passed: (\d+)\s*\n"
    r"smpselftest_tasks_finished: \d+\s*\n"
    r"smpselftest_tasks_expected: \d+\s*\n"
    r"smpselftest: pass\s*$",
    re.MULTILINE,
)


def wait_for_serial_text(
    serial_path: Path, needle: str, timeout: float, start_offset: int = 0
) -> tuple[bool, int]:
    deadline = time.monotonic() + timeout
    offset = start_offset
    while time.monotonic() < deadline:
        if serial_path.is_file():
            with serial_path.open("r", encoding="utf-8", errors="replace") as handle:
                handle.seek(offset)
                chunk = handle.read()
                offset = handle.tell()
            if needle in chunk:
                return True, offset
        time.sleep(0.1)
    return False, offset


def expected_checksum(work_items: int) -> int:
    checksum = 0
    for index in range(work_items):
        value = index + 1
        for _ in range(128):
            value = (value * CHECKSUM_MULTIPLIER + CHECKSUM_INCREMENT) & U64_MASK
        checksum = (checksum + value) & U64_MASK
    return checksum


def transcript_filename(pid: int, cpus: int) -> str:
    alphabet = "0123456789abcdefghijklmnopqrstuvwxyz"

    def base36(value: int) -> str:
        encoded = ""
        while value:
            value, remainder = divmod(value, 36)
            encoded = alphabet[remainder] + encoded
        return encoded or "0"

    prefix = f"smp{base36(cpus)}"
    suffix_length = 8 - len(prefix)
    if suffix_length < 1:
        raise ValueError(f"CPU count cannot fit in a FAT 8.3 filename: {cpus}")
    pid_token = base36(pid)[-suffix_length:].rjust(suffix_length, "0")
    return f"{prefix}{pid_token}.txt"


def field_value(transcript: str, name: str) -> int:
    match = re.search(rf"^{re.escape(name)}: (\d+)\s*$", transcript, re.MULTILINE)
    if not match:
        raise ValueError(f"missing {name} in transcript")
    return int(match.group(1))


TPM_DIAG_COMMANDS = ("tpm status", "tpm pcrs 7", "tpm selftest", "tpm eventlog")


def validate_tpm_transcript(
    transcript: str, tpm_device: str, firmware: str = "bios"
) -> None:
    expected_transport = "tis-fifo" if tpm_device == "tpm-tis" else "crb"
    required = (
        "tpm_ready: yes",
        f"tpm_transport: {expected_transport}",
        "tpm selftest: pass",
    )
    for text in required:
        if text not in transcript:
            raise ValueError(f"TPM diagnostics missing '{text}'")
    secure_boot = EXPECTED_SECURE_BOOT[firmware]
    if not any(text in transcript for text in secure_boot):
        raise ValueError(f"TPM diagnostics missing '{secure_boot[0]}'")
    if not re.search(r"^pcr07 sha256: [0-9a-f]{64}\s*$", transcript, re.MULTILINE):
        raise ValueError("TPM diagnostics did not report a PCR 7 digest")
    if not re.search(r"^#\d+ pcr7 ok ", transcript, re.MULTILINE):
        raise ValueError("TPM event log did not report a successful PCR 7 measurement")


def validate_transcript(
    transcript: str, cpus: int, work_items: int, iterations: int
) -> None:
    expected_aps = cpus - 1
    expected_fields = {
        "smp_total_cpus": cpus,
        "smp_target_aps": expected_aps,
        "smp_online_aps": expected_aps,
        "smp_slot_count": expected_aps,
    }
    for field, expected in expected_fields.items():
        actual = field_value(transcript, field)
        if actual != expected:
            raise ValueError(f"{field}: expected {expected}, got {actual}")

    slots = [
        (
            int(cpu), state, int(online), int(timer_ticks), int(scheduler_kicks),
            int(work), int(task0_turns), int(task1_turns), int(task2_turns),
            int(task2_wakeups), int(tasks_reaped)
        )
        for cpu, state, online, timer_ticks, scheduler_kicks, work, task0_turns, task1_turns,
            task2_turns, task2_wakeups, tasks_reaped
        in SLOT_PATTERN.findall(transcript)
    ]
    if expected_aps == 0:
        if slots:
            raise ValueError("single-CPU run unexpectedly reported AP slots")
        if WORK_PATTERN.search(transcript):
            raise ValueError("single-CPU run unexpectedly completed AP work")
        return

    selftest = SELFTEST_PATTERN.search(transcript)
    if not selftest:
        raise ValueError("smpselftest status did not report pass")
    selftest_aps, selftest_passed = int(selftest.group(1)), int(selftest.group(2))
    if selftest_aps != expected_aps or selftest_passed != expected_aps:
        raise ValueError(
            f"smpselftest: expected {expected_aps}/{expected_aps} APs to pass, "
            f"got {selftest_passed}/{selftest_aps}"
        )

    work_results = WORK_PATTERN.findall(transcript)
    if len(work_results) != iterations:
        raise ValueError(
            f"expected {iterations} work results, found {len(work_results)}"
        )
    checksum = expected_checksum(work_items)
    for requested, completed, actual_checksum in work_results:
        if int(requested) != work_items or int(completed) != work_items:
            raise ValueError(
                f"work batch incomplete: requested {requested}, completed {completed}"
            )
        if int(actual_checksum) != checksum:
            raise ValueError(
                f"work checksum mismatch: expected {checksum}, got {actual_checksum}"
            )

    participating_cpus = set()
    timer_cpus = set()
    scheduler_cpus = set()
    task_cpus = set()
    sleep_wake_cpus = set()
    reaped_task_cpus = set()
    status_snapshots = transcript.count("smp_slots:")
    if status_snapshots < iterations:
        raise ValueError(
            f"expected at least {iterations} SMP status snapshots, found {status_snapshots}"
        )
    for (
        cpu, state, online, timer_ticks, scheduler_kicks, work,
        task0_turns, task1_turns, task2_turns, task2_wakeups, tasks_reaped
    ) in slots:
        if state != "online" or online != 1:
            raise ValueError(f"AP {cpu} was not reported online")
        if work > 0:
            participating_cpus.add(cpu)
        if timer_ticks > 0:
            timer_cpus.add(cpu)
        if scheduler_kicks > 0:
            scheduler_cpus.add(cpu)
        if task0_turns > 0 and task1_turns > 0:
            task_cpus.add(cpu)
        if task2_turns > 0 and task2_wakeups > 0:
            sleep_wake_cpus.add(cpu)
        if tasks_reaped > 0:
            reaped_task_cpus.add(cpu)

    if len(participating_cpus) != expected_aps:
        raise ValueError(
            f"expected all {expected_aps} APs to process work, "
            f"observed {len(participating_cpus)}"
        )
    if len(timer_cpus) != expected_aps:
        raise ValueError(
            f"expected all {expected_aps} APs to receive local timer ticks, "
            f"observed {len(timer_cpus)}"
        )
    if len(scheduler_cpus) != expected_aps:
        raise ValueError(
            f"expected all {expected_aps} APs to record scheduler quanta, "
            f"observed {len(scheduler_cpus)}"
        )
    if len(task_cpus) != expected_aps:
        raise ValueError(
            f"expected both non-yielding tasks to run on all {expected_aps} APs, "
            f"observed {len(task_cpus)} APs with task turns"
        )
    if len(sleep_wake_cpus) != expected_aps:
        raise ValueError(
            f"expected sleep/wakeup to complete on all {expected_aps} APs, "
            f"observed {len(sleep_wake_cpus)}"
        )
    if len(reaped_task_cpus) != expected_aps:
        raise ValueError(
            f"expected a terminated task stack to be reaped on all {expected_aps} APs, "
            f"observed {len(reaped_task_cpus)}"
        )


class QmpClient:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.reader = sock.makefile("rb")
        self.next_id = 1
        greeting = json.loads(self.reader.readline())
        if "QMP" not in greeting:
            raise RuntimeError("QEMU did not send a QMP greeting")
        self.execute("qmp_capabilities")

    def execute(self, command: str, arguments: dict | None = None) -> dict:
        request_id = self.next_id
        self.next_id += 1
        request = {"execute": command, "id": request_id}
        if arguments is not None:
            request["arguments"] = arguments
        self.sock.sendall(json.dumps(request).encode("utf-8") + b"\r\n")
        while True:
            line = self.reader.readline()
            if not line:
                raise RuntimeError("QMP connection closed unexpectedly")
            response = json.loads(line)
            if response.get("id") != request_id:
                continue
            if "error" in response:
                raise RuntimeError(f"QMP {command} failed: {response['error']}")
            return response.get("return", {})

    def press(self, character: str) -> None:
        if character == " ":
            key = "spc"
        elif character == "-":
            key = "minus"
        elif character == ".":
            key = "dot"
        elif character.isdigit() or "a" <= character <= "z":
            key = character
        else:
            raise ValueError(f"unsupported QEMU key character: {character!r}")
        self.execute(
            "send-key",
            {"keys": [{"type": "qcode", "data": key}], "hold-time": 50},
        )

    def press_qcode(self, key: str) -> None:
        self.execute(
            "send-key",
            {"keys": [{"type": "qcode", "data": key}], "hold-time": 50},
        )

    def type_command(self, command: str) -> None:
        for character in command:
            self.press(character)
            time.sleep(0.05)
        self.press_enter()

    def press_enter(self) -> None:
        self.execute(
            "send-key",
            {"keys": [{"type": "qcode", "data": "ret"}], "hold-time": 50},
        )


def validate_qmp_text(name: str, value: str) -> None:
    unsupported = sorted(
        {
            character
            for character in value
            if not (character.isdigit() or ("a" <= character <= "z") or character in " .-")
        }
    )
    if unsupported:
        unsupported_display = "".join(unsupported)
        raise ValueError(
            f"{name} contains unsupported characters for QMP typing: {unsupported_display!r}; "
            "use lowercase letters, digits, space, dot, or dash"
        )


def connect_qmp(path: Path, process: subprocess.Popen, timeout: float) -> QmpClient:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early with status {process.returncode}")
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(str(path))
            sock.settimeout(10)
            return QmpClient(sock)
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"timed out connecting to QEMU QMP socket: {last_error}")


def startup_entry_index(startup_entry: str) -> int:
    mapping = {
        "desktop": 0,
        "terminal": 1,
        "recovery": 2,
        "diagnostic": 3,
    }
    try:
        return mapping[startup_entry]
    except KeyError as error:
        raise ValueError(f"unsupported startup entry: {startup_entry}") from error


def select_startup_entry(client: QmpClient, startup_entry: str) -> None:
    index = startup_entry_index(startup_entry)
    if index == 0:
        return

    # Limine keeps the menu active briefly; wait a moment, then move and confirm.
    time.sleep(0.5)
    for _ in range(index):
        client.press_qcode("down")
        time.sleep(0.1)
    client.press_enter()


def drive_owner_gate_if_prompted(
    client: QmpClient,
    serial_path: Path,
    owner_username: str | None,
    owner_password: str | None,
    startup_delay: float,
) -> None:
    if owner_username:
        validate_qmp_text("owner username", owner_username)
    if owner_password:
        validate_qmp_text("owner password", owner_password)

    # Poll only during startup delay; proceed even if no owner prompts appear.
    timeout = max(0.0, startup_delay)
    cursor = 0
    username_sent = False
    password_sent = False
    confirm_sent = False
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        wait_budget = min(0.3, max(0.0, deadline - time.monotonic()))
        if wait_budget <= 0:
            break
        found, cursor = wait_for_serial_text(
            serial_path, "Owner username:", wait_budget, cursor
        )
        if found and owner_username and not username_sent:
            client.type_command(owner_username)
            username_sent = True
            continue

        found, cursor = wait_for_serial_text(
            serial_path, "Owner password:", wait_budget, cursor
        )
        if found and owner_password and not password_sent:
            client.type_command(owner_password)
            password_sent = True
            continue

        found, cursor = wait_for_serial_text(
            serial_path, "Confirm password:", wait_budget, cursor
        )
        if found and owner_password and not confirm_sent:
            client.type_command(owner_password)
            confirm_sent = True
            continue

        found, cursor = wait_for_serial_text(
            serial_path, "Owner unlocked.", wait_budget, cursor
        )
        if found:
            return


def qemu_binary(requested: str) -> str:
    if requested == "qemu-system-x86_64":
        distro_qemu = Path("/usr/bin/qemu-system-x86_64")
        if distro_qemu.is_file() and os.access(distro_qemu, os.X_OK):
            return str(distro_qemu)
    resolved = shutil.which(requested)
    if resolved:
        return resolved
    if requested == "qemu-system-x86_64":
        fallback = Path("/usr/local/bin/qemu-system-x86_64")
        if fallback.is_file() and os.access(fallback, os.X_OK):
            return str(fallback)
    raise FileNotFoundError(f"QEMU executable not found: {requested}")


def qemu_arguments(
    qemu: str,
    iso: Path,
    cpus: int,
    qmp_path: Path,
    serial_path: Path,
    interactive_login: bool,
    tpm_socket: Path | None = None,
    system_volume: bool = False,
    relative_mouse: bool = False,
    tpm_device: str = "tpm-crb",
    firmware: str = "bios",
    ovmf_vars: Path | None = None,
    no_ps2: bool = False,
    keyboard_second_xhci: bool = False,
) -> list[str]:
    command = [
        qemu,
        "-cdrom",
        str(iso),
        "-boot",
        "order=d",
        "-m",
        "3G",
        "-smp",
        str(cpus),
        "-vga",
        "vmware",
        "-netdev",
        "user,id=net0",
        "-device",
        "e1000,netdev=net0",
        "-device",
        "isa-debug-exit,iobase=0xf4,iosize=0x04",
        "-device",
        "qemu-xhci,id=xhci",
        "-serial",
        f"file:{serial_path}",
        "-display",
        "sdl" if interactive_login else "none",
        "-qmp",
        f"unix:{qmp_path},server=on,wait=off",
    ]
    # OVMF Secure Boot needs q35 with SMM; its disks go on an added PIIX4 IDE
    # controller so the kernel's legacy IDE probe still finds them.
    q35 = firmware == "secureboot" or no_ps2
    if q35:
        machine = "q35"
        if firmware == "secureboot":
            machine += ",smm=on"
        if no_ps2:
            # Model PCs without an 8042 so input must come from the xHCI keyboard.
            machine += ",i8042=off"
        command.extend(["-machine", machine, "-device", "piix4-ide,id=pide"])
    elif tpm_socket:
        command.extend(["-machine", "pc"])
    if firmware != "bios":
        code = OVMF_DIR / (
            "OVMF_CODE_4M.secboot.fd" if q35 else "OVMF_CODE_4M.fd"
        )
        if q35:
            command.extend(["-global", "driver=cfi.pflash01,property=secure,value=on"])
        command.extend(
            [
                "-drive",
                f"if=pflash,format=raw,unit=0,readonly=on,file={code}",
                "-drive",
                f"if=pflash,format=raw,unit=1,file={ovmf_vars}",
            ]
        )
    ide_bus = "pide.0" if q35 else "ide.0"

    if tpm_socket:
        command.extend(
            [
                "-chardev",
                f"socket,id=chrtpm,path={tpm_socket}",
                "-tpmdev",
                "emulator,id=tpm0,chardev=chrtpm",
                "-device",
                f"{tpm_device},tpmdev=tpm0",
            ]
        )

    if relative_mouse:
        command.extend(["-device", "usb-mouse,bus=xhci.0"])
    else:
        command.extend(["-device", "usb-tablet,bus=xhci.0"])

    if keyboard_second_xhci:
        command.extend(["-device", "qemu-xhci,id=xhci2"])
    if relative_mouse or no_ps2:
        keyboard_bus = "xhci2.0" if keyboard_second_xhci else "xhci.0"
        command.extend(["-device", f"usb-kbd,bus={keyboard_bus}"])

    if system_volume:
        if tpm_socket or q35:
            command.extend(
                [
                    "-drive",
                    f"id=systemdisk,if=none,file={SYSTEM_DISK},media=disk,format=qcow2",
                    "-device",
                    f"ide-hd,drive=systemdisk,bus={ide_bus},unit=0",
                ]
            )
        else:
            command.extend(
                [
                    "-drive",
                    f"file={SYSTEM_DISK},if=ide,index=0,media=disk,format=qcow2",
                ]
            )

    if tpm_socket or q35:
        command.extend(
            [
                "-drive",
                f"id=shareddisk,if=none,file=fat:rw:{SHARED_DIR},format=raw",
                "-device",
                f"ide-hd,drive=shareddisk,bus={ide_bus},unit=1",
            ]
        )
    else:
        command.extend(
            [
                "-drive",
                f"file=fat:rw:{SHARED_DIR},format=raw,if=ide,index=1",
            ]
        )
    return command


def start_swtpm(pid: int, cpus: int) -> tuple[subprocess.Popen, Path]:
    swtpm = shutil.which("swtpm")
    if not swtpm:
        raise FileNotFoundError("--tpm requires swtpm (install swtpm and swtpm-tools)")

    tpm_dir = BUILD_DIR / "swtpm"
    state_dir = tpm_dir / "smp-state"
    state_dir.mkdir(parents=True, exist_ok=True)
    socket_path = tpm_dir / f"smp-qemu-{pid}-{cpus}.sock"
    log_path = tpm_dir / f"smp-qemu-{pid}-{cpus}.log"
    if socket_path.exists():
        raise FileExistsError(f"refusing to overwrite TPM socket path: {socket_path}")

    with log_path.open("wb") as log_file:
        process = subprocess.Popen(
            [
                swtpm,
                "socket",
                "--tpm2",
                "--tpmstate",
                f"dir={state_dir}",
                "--ctrl",
                f"type=unixio,path={socket_path}",
                "--log",
                "level=20",
            ],
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )

    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if socket_path.is_socket():
            return process, socket_path
        if process.poll() is not None:
            detail = log_path.read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(
                f"swtpm exited during startup with status {process.returncode}: "
                f"{detail[-2000:]}"
            )
        time.sleep(0.05)

    process.terminate()
    process.wait(timeout=5)
    raise TimeoutError(f"swtpm did not create its control socket: {socket_path}")


def ensure_system_volume() -> None:
    if SYSTEM_DISK.exists():
        return

    qemu_img = shutil.which("qemu-img")
    if not qemu_img:
        raise FileNotFoundError("--system-vol requires qemu-img (install qemu-utils)")
    result = subprocess.run(
        [qemu_img, "create", "-f", "qcow2", str(SYSTEM_DISK), "256M"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"failed to create system volume {SYSTEM_DISK}: "
            f"{result.stderr.strip() or result.stdout.strip()}"
        )
    print(
        f"Created system volume {SYSTEM_DISK}; format it in-guest before mounting /system.",
        flush=True,
    )


def require_production_build() -> None:
    cache_path = BUILD_DIR / "CMakeCache.txt"
    if not cache_path.is_file():
        raise FileNotFoundError(
            "--prod requires a production-built ISO; run ./build.sh --prod --shared-source qemu"
        )
    configured = re.search(
        r"^CITADEL_PRODUCTION:BOOL=(ON|OFF)\s*$",
        cache_path.read_text(encoding="utf-8", errors="replace"),
        re.MULTILINE,
    )
    if not configured or configured.group(1) != "ON":
        raise ValueError(
            "--prod requires a production-built ISO; run ./build.sh --prod --shared-source qemu"
        )


def run_case(
    qemu: str,
    iso: Path,
    cpus: int,
    work_items: int,
    iterations: int,
    startup_delay: float,
    run_timeout: float,
    interactive_login: bool,
    tpm: bool,
    tpm_device: str,
    system_volume: bool,
    relative_mouse: bool,
    startup_entry: str,
    owner_username: str | None,
    owner_password: str | None,
    firmware: str = "bios",
    no_ps2: bool = False,
    keyboard_second_xhci: bool = False,
) -> None:
    transcript_path = SHARED_DIR / transcript_filename(os.getpid(), cpus)
    serial_path = PROJECT_DIR / "build" / f"smp-qemu-{os.getpid()}-{cpus}.serial.log"
    if transcript_path.exists():
        raise FileExistsError(f"refusing to overwrite {transcript_path}")

    with tempfile.TemporaryDirectory(prefix="citadel-smp-qmp-") as temp_dir:
        qmp_path = Path(temp_dir) / "qmp.sock"
        tpm_process: subprocess.Popen | None = None
        tpm_socket: Path | None = None
        process: subprocess.Popen | None = None
        client: QmpClient | None = None
        try:
            ovmf_vars: Path | None = None
            if firmware != "bios":
                template = (
                    SECUREBOOT_VARS
                    if firmware == "secureboot"
                    else OVMF_DIR / "OVMF_VARS_4M.fd"
                )
                ovmf_vars = Path(temp_dir) / "OVMF_VARS.fd"
                shutil.copyfile(template, ovmf_vars)
            if tpm:
                tpm_process, tpm_socket = start_swtpm(os.getpid(), cpus)
            command = qemu_arguments(
                qemu,
                iso,
                cpus,
                qmp_path,
                serial_path,
                interactive_login,
                tpm_socket,
                system_volume,
                relative_mouse,
                tpm_device,
                firmware,
                ovmf_vars,
                no_ps2,
                keyboard_second_xhci,
            )
            print(
                f"Starting QEMU SMP case: {cpus} CPU(s), firmware={firmware}",
                flush=True,
            )
            process = subprocess.Popen(command)
            client = connect_qmp(qmp_path, process, timeout=30)
            if firmware != "bios" and startup_entry_index(startup_entry) != 0:
                # OVMF takes seconds to reach Limine; menu keys sent earlier are lost.
                wait_for_serial_text(serial_path, "BdsDxe: starting", 120)
                time.sleep(1.5)
            select_startup_entry(client, startup_entry)
            if startup_entry == "diagnostic":
                wait_for_serial_text(serial_path, "Boot trace enabled", startup_delay)
            drive_owner_gate_if_prompted(
                client,
                serial_path,
                owner_username,
                owner_password,
                startup_delay,
            )
            if process.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited during startup with status {process.returncode}"
                )

            if interactive_login:
                if not sys.stdin.isatty():
                    raise RuntimeError(
                        "--interactive-login requires running from an interactive terminal"
                    )
                input(
                    f"Complete desktop login in the QEMU window for the {cpus}-CPU case, "
                    "then press Enter here to run the SMP commands..."
                )
                if process.poll() is not None:
                    raise RuntimeError(
                        f"QEMU exited during desktop login with status {process.returncode}"
                    )

            # Slower boots (OVMF under TCG) can still be starting the session after
            # owner unlock; typing earlier drops keystrokes.
            ready_marker = (
                "Entering console-only startup path"
                if startup_entry == "diagnostic"
                else "Entering main loop"
            )
            wait_for_serial_text(serial_path, ready_marker, startup_delay)
            time.sleep(1)
            if startup_entry == "desktop":
                # The desktop binds an unmodified T key to opening the terminal.
                client.press("t")
                time.sleep(2)
            client.type_command("clear")
            time.sleep(2)
            if cpus == 1:
                client.type_command("smpstat all")
                client.type_command(f"saveterm {transcript_path.name}")
            else:
                client.type_command("smpselftest")
                for _ in range(iterations):
                    client.type_command(f"smpwork {work_items}")
                    client.type_command("smpstat all")
                client.type_command("smpselftest status")
                if tpm:
                    for tpm_command in TPM_DIAG_COMMANDS:
                        client.type_command(tpm_command)
                        # TPM commands run in the keyboard handler; keys sent
                        # before they finish are dropped.
                        time.sleep(3)
                client.type_command(f"saveterm {transcript_path.name}")

            deadline = time.monotonic() + run_timeout
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError(
                        f"QEMU exited before saving its transcript "
                        f"(status {process.returncode})"
                    )
                if transcript_path.is_file() and transcript_path.stat().st_size > 0:
                    transcript = transcript_path.read_text(
                        encoding="utf-8", errors="replace"
                    )
                    if "smp_total_cpus:" in transcript:
                        break
                time.sleep(1)
            else:
                screenshot_path = serial_path.with_suffix(".png")
                client.execute("screendump", {"filename": str(screenshot_path)})
                raise TimeoutError(
                    f"QEMU did not save an SMP transcript within {run_timeout:g}s; "
                    f"inspect {screenshot_path} and {serial_path}"
                )

            validate_transcript(transcript, cpus, work_items, iterations)
            if tpm and cpus > 1:
                validate_tpm_transcript(transcript, tpm_device, firmware)
            print(f"PASS: {cpus} CPU(s), {cpus - 1} AP(s)", flush=True)
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
        description="Run the terminal-driven SMP smoke test in QEMU."
    )
    parser.add_argument("--iso", type=Path, default=DEFAULT_ISO)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--cpus", type=int, nargs="+", default=[1, 2, 4])
    parser.add_argument("--work-items", type=int, default=WORK_ITEMS)
    parser.add_argument("--iterations", type=int, default=ITERATIONS)
    parser.add_argument("--startup-delay", type=float, default=45)
    parser.add_argument("--run-timeout", type=float, default=180)
    parser.add_argument(
        "--interactive-login",
        action="store_true",
        help="show a QEMU desktop window and wait for manual login before sending test commands",
    )
    parser.add_argument(
        "-r",
        "--run",
        action="store_true",
        help="accepted for build.sh compatibility; this runner always launches QEMU",
    )
    parser.add_argument(
        "--tpm",
        action="store_true",
        help="launch swtpm and attach a TPM 2.0 device",
    )
    parser.add_argument(
        "--tpm-tis",
        action="store_true",
        help="with --tpm, attach a TIS/FIFO TPM (tpm-tis) instead of CRB",
    )
    parser.add_argument(
        "--system-vol",
        action="store_true",
        help="attach build/system.qcow2, creating a 256 MB disk if missing",
    )
    parser.add_argument(
        "--relmouse",
        action="store_true",
        help="use relative USB mouse and keyboard instead of an absolute tablet",
    )
    parser.add_argument(
        "--prod",
        action="store_true",
        help="require an ISO built with CITADEL_PRODUCTION=ON",
    )
    parser.add_argument(
        "--check-log",
        type=Path,
        help="validate a previously captured terminal transcript instead of running QEMU",
    )
    parser.add_argument(
        "--check-cpus",
        type=int,
        help="CPU count for --check-log (defaults to 4)",
    )
    parser.add_argument(
        "--startup-entry",
        choices=["desktop", "terminal", "recovery", "diagnostic"],
        default="desktop",
        help="Limine menu entry to boot before running SMP commands",
    )
    parser.add_argument(
        "--owner-username",
        help="optional owner username for pre-desktop owner-gate prompts",
    )
    parser.add_argument(
        "--owner-password",
        help="optional owner password for pre-desktop owner-gate prompts",
    )
    parser.add_argument(
        "--firmware",
        choices=["bios", "uefi", "secureboot"],
        default="bios",
        help="boot with legacy BIOS, OVMF UEFI, or OVMF Secure Boot using the "
        "lab keys from tools/secureboot/make_lab_keys.sh",
    )
    parser.add_argument(
        "--no-ps2",
        action="store_true",
        help="disable the i8042 PS/2 controller (q35) and attach a USB keyboard, "
        "like modern PCs without legacy PS/2",
    )
    parser.add_argument(
        "--keyboard-second-xhci",
        action="store_true",
        help="with --no-ps2, attach the keyboard to a second xHCI controller",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.work_items <= 0 or args.iterations <= 0:
        print("work item count and iteration count must be positive", file=sys.stderr)
        return 2
    if args.keyboard_second_xhci and not args.no_ps2:
        print("--keyboard-second-xhci requires --no-ps2", file=sys.stderr)
        return 2

    try:
        if args.check_log:
            cpus = args.check_cpus or 4
            transcript = args.check_log.read_text(encoding="utf-8", errors="replace")
            validate_transcript(transcript, cpus, args.work_items, args.iterations)
            print(f"PASS: transcript validates for {cpus} CPU(s)")
            return 0

        if args.check_cpus is not None:
            print("--check-cpus requires --check-log", file=sys.stderr)
            return 2
        if any(cpus < 1 or cpus > 256 for cpus in args.cpus):
            print("CPU counts must be from 1 to 256", file=sys.stderr)
            return 2
        if not args.iso.is_file():
            print(f"ISO not found: {args.iso}", file=sys.stderr)
            return 2
        if not SHARED_DIR.is_dir():
            print(f"QEMU shared directory not found: {SHARED_DIR}", file=sys.stderr)
            return 2
        if args.prod:
            require_production_build()
        if args.system_vol:
            ensure_system_volume()

        qemu = qemu_binary(args.qemu)
        for cpus in args.cpus:
            run_case(
                qemu,
                args.iso.resolve(),
                cpus,
                args.work_items,
                args.iterations,
                args.startup_delay,
                args.run_timeout,
                args.interactive_login,
                args.tpm or args.tpm_tis,
                "tpm-tis" if args.tpm_tis else "tpm-crb",
                args.system_vol,
                args.relmouse,
                args.startup_entry,
                args.owner_username,
                args.owner_password,
                args.firmware,
                args.no_ps2,
                args.keyboard_second_xhci,
            )
    except (FileNotFoundError, OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
