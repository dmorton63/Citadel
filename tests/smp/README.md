# SMP QEMU smoke test

Build the ISO with the QEMU host-share mount enabled, then run the test matrix:

```sh
./build.sh --shared-source qemu
python3 tools/smp_qemu_smoke.py --interactive-login
```

The runner starts separate 1-, 2-, and 4-vCPU QEMU boots, opens the terminal
with the desktop's `T` shortcut, starts `smpselftest`, drives the
`smpwork`/`smpstat` loop, and finishes with `smpselftest status` through QMP
keyboard input. With `--interactive-login`, each boot opens a visible QEMU
desktop; log in there, then press Enter in the terminal running the script.
The headless default is suitable only when the boot requires no manual login.
The runner accepts `-r`/`--run` for compatibility with `build.sh`; QEMU always
runs when the smoke-test command is invoked. `--tpm` starts `swtpm` and attaches
TPM 2.0 over CRB; `--tpm-tis` attaches it over TIS/FIFO instead. With either
option, multi-CPU cases also run `tpm status`, `tpm pcrs 7`, `tpm selftest`,
and `tpm eventlog`, and require a ready TPM on the expected transport, a passing
self-test, and a successful PCR 7 measurement. `--system-vol` attaches `build/system.qcow2`, creating a blank 256 MB
disk if needed. `--relmouse` selects a relative USB mouse and keyboard.

Production is a build-time setting, not a QEMU argument. Build a production
ISO first, then pass `--prod` to require that the build cache is configured for
production:

```sh
./build.sh --prod --shared-source qemu
python3 tools/smp_qemu_smoke.py --cpus 1 2 --interactive-login --prod --tpm --system-vol --relmouse -r
```

`--prod` checks `build/CMakeCache.txt`; it does not alter an already-built ISO.
The runner saves transcripts under unique, FAT 8.3-compatible
names such as `shared/smp40001.txt` (CPU count and process ID encoded in the
filename).
The 1-vCPU case verifies the no-AP baseline; the 2- and 4-vCPU cases require
all APs online, all work batches complete, a deterministic checksum, and every
AP to process work.

Normal boots run one `ap-worker` task per AP. It drains the shared AP work
queue, blocks when the queue is empty, and is unblocked by the next enqueue;
with no ready task, the AP idle loop halts until its next local timer tick.
Scheduler self-test tasks run only on demand through `smpselftest`, which
starts three bounded tasks per AP and returns immediately. Terminal commands
run in keyboard-IRQ context, where the PIT clock cannot advance, so the command
does not wait for results; use `smpselftest status` afterwards.

On each AP, two self-test tasks spin without yielding and pass only after each
has observed the other advance, proving that local LAPIC timer interrupts
preempt and resume task contexts (`t0`, `t1`). A third task sleeps, is woken by
timer ticks, and exits (`t2`, `w2`); terminated stacks are reclaimed
(`reaped`). The BSP PIT and AP-local LAPIC timers are acknowledged before task
switching, and scheduler-quantum counters are checked separately.

Desktop initialization and its first render stay on the boot stack. Once
initialized, the driver-poll, event, and render loop runs as `desktop-runtime`,
pinned to CPU 0 with a 64 KiB stack. This keeps mutable window state owned by
one task while AP work remains isolated from the desktop.

To validate an existing capture without starting QEMU:

```sh
python3 tools/smp_qemu_smoke.py --check-log shared/citadel.txt --check-cpus 4
```

The default startup delay is 45 seconds. Increase it with `--startup-delay` on
slower hosts. The ISO must have been built with `--shared-source qemu` so
`/shared` maps to the host `shared/` directory.

For unattended boots that must bypass graphical login wait states, boot the
Terminal entry and optionally provide owner credentials:

```sh
python3 tools/smp_qemu_smoke.py --cpus 2 4 --startup-entry terminal --owner-password <password>
```

If the owner gate requires a username prompt, add `--owner-username <username>`.
QMP typing currently supports lowercase letters, digits, spaces, dots, and
dashes in automated credentials.

## Manual terminal-driven SMP validation

When GUI/QMP automation is not desirable, launch QEMU directly and run SMP
commands in the in-guest terminal:

```sh
./run.sh --smp 1
./run.sh --smp 2
./run.sh --smp 3
./run.sh --smp 4
./run.sh --smp 8
```

Use `./run.sh --smp <N> --print-cmd` to print the exact QEMU invocation before
launching.

Recommended in-guest command sequence:

```text
smpstat all
smpselftest
loop 10 times
smpwork 10000
smpstat all
endloop smpselftest status
saveterm <name>.txt
```

Expected behavior:

- `N=1`: `smp_target_aps: 0`, `smp_online_aps: 0`, and `smpwork` reports AP
  workers unavailable.
- `N>1`: `smp_target_aps: N-1`, `smp_online_aps: N-1`, each loop iteration
  reports `smpwork_completed: 10000`, and checksum stays
  `13875592363411389448`. After `smpselftest`, each AP's `smpstat all` line
  shows positive `t0`, `t1`, `t2`, `w2`, and `reaped` counters, and
  `smpselftest status` reports `smpselftest: pass` with every AP passed.

Validated transcript artifacts in `shared/`:

- `smp1test.txt` (1 CPU baseline)
- `smptest2.txt` (2 CPUs)
- `smp3test.txt` (3 CPUs)
- `smp-qemu-1-4.txt` (4 CPUs)
- `smp8text.txt` (8 CPUs)

These named captures predate the task-context test and do not contain `t0`/`t1`
counters, so they cannot pass the current validator. Generate fresh captures
with `tools/smp_qemu_smoke.py` for current scheduler evidence.
