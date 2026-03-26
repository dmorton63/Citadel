# TODO (Main)

## Branding & Identity
- [ ] Refresh UI/boot logo artwork so the visual mark reads "CITADEL" while the system name/code remains QAIOSPLUSV1.
- [ ] Audit only the visible logo placements (splash, desktop, installer) for the CITADEL art update—no internal renames.
- [ ] Produce an asset inventory (sizes/formats/locations) so art swaps are scripted instead of manual edits.
- [ ] Document a style guide (colors, typography, animation beats) that desktop widgets and boot flows can reference.

## Graphics & Media Pipeline
- [ ] Implement an Image Reader service capable of loading bitmap/PNG assets so the desktop can render icons and other artwork.
  - [ ] Initial format targets: `.ico`, `.png`, `.bmp`.
  - [ ] Sketch a modular decode pipeline (stream reader + pixel surface abstraction) so future formats can plug in without rewriting call sites.
  - [ ] Define JSON-driven format descriptors (magic, module list, pipeline verbs) plus a dispatcher that instantiates modules and executes those recipes.
  - [ ] Design the `IMGModule` interface, module registry, and `ImageContext` so pipeline steps like `read_header`, `decompress`, `to_rgba` map cleanly to decoder responsibilities.
  - [ ] Add a unit-test corpus of tiny icons and malformed headers so the decoder path is robust before UI hooks it up.
  - [ ] Build a preview CLI command that dumps decoded surfaces to verify the pipeline without launching the desktop.

## Shutdown Subsystem Follow-ups  [ DONE - COMPLETED]
- [x] Register a QFileSystem shutdown listener (using `registerSubsystem`) that walks open handles and flushes dirty buffers before acknowledging a shutdown request.
- [x] Register future app/runtime subsystems (editors, desktop apps, etc.) so they can veto shutdown until their own unsaved state is resolved.


# TODO (Command Center)
  [ ] Build a structure to hold the commands 
  [ ] Command Parser
  [ ] Command Execution
  [ ] and anything else I've forgotten here.
  [x] Temporary console command (`ls`) so we can inspect mounts until the full command center lands.
  [ ] Ship a starter command set (help, clear, startx, mount, cat) so the terminal is useful on day one.
  [ ] Persist a registry/alias map so commands can be extended or overridden without editing the kernel image.
  [ ] Add a test harness that runs command handlers against captured console transcripts to avoid regressions.
  [ ] Add command metadata (usage strings, argument schema) for auto-generated help and validation.
  [ ] Introduce a history buffer plus `history`/`!n` recall for faster debugging.
  [ ] Support command scripts (`source file.cmd`) so repetitive bootstraps can be automated.
  [ ] Expose an IPC hook so future GUI shells can reuse the parser without reimplementing every command.

# TODO (Modular Driver Packages)
  [ ] Define a `.drv`/`.dll` packaging spec (header, metadata, relocation table) for loadable kernel modules.
  [ ] Build a loader that can fetch those binaries from disk/ramdisk on demand and resolve their dependencies.
  [ ] Provide lifecycle hooks (`init`, `start`, `stop`) so modules can register/unregister drivers at runtime.
  [ ] Update build scripts to emit module bundles separately from the core kernel image.
  [ ] Reserve a namespace/versioning scheme so incompatible modules are rejected cleanly.
  [ ] Create a signing/hash check so only trusted modules load during secure boot.
  [ ] Add `module list/load/unload` console commands that call the new APIs.

# TODO (Console / Boot Modes)
  [ ] Replace Limine's basic terminal view with an in-kernel terminal that mirrors serial.log output so we get on-screen feedback without relying on external tailing.
  [ ] Add a `startx`-style console command that bootstraps the framebuffer, window manager, and desktop when running in TERMINAL startup mode.
  [ ] Provide a `stopx` path that gracefully tears down the desktop and returns to the console-only state.
  [ ] Persist chosen startup mode (`TERMINAL`, `DESKTOP`, `SAFE`) in `startup.cfg` and surface it via `showmode` command.
  [ ] Pipe boot logs into a ring buffer that both the console and desktop log viewer can tail.

# TODO (File SyStem)
- [ ] Expand `QFileSystem` device discovery so it can enumerate block devices beyond the ramdisk (e.g., SATA/NVMe exposed by QDrivers).
  - [x] Define a hardware-backed volume descriptor that the filesystem mount manager can register once a driver announces media.
  - [x] Provide a `QKStorage::registerBlockDevice` helper so drivers can surface volumes without hand-rolling the VFS plumbing.
  - [x] Add a probing path that requests attached storage info from the hardware abstraction layer during boot and on hotplug events.
  - [x] Hook `QKDrvManager` polling so auto-mount volumes registered by drivers are mounted as soon as hardware reports in.
- [ ] Ensure naming standards stay consistent (volumes stay `QFS_*`, drivers stay `QDRV_*`) when new devices are surfaced.
- [ ] Implement `mount`/`umount` syscalls plus console commands, including fstab-style persistence.
- [ ] Add writeback caching + explicit `sync` command so removable media can be ejected safely.
- [ ] Provide a VFS-backed `stat` API so tooling can query file metadata without touching filesystem internals.

# TODO (CPU Core Manager)

# TODO (Quantum Engine)

# TODO (Networking)
  [ ] Implement a minimal DHCPv4 client and run it at boot to auto-configure IPv4 (IP/mask/gateway/DNS); fall back to manual `ip set` if DHCP fails.
  [ ] Add `ip dhcp` to renew DHCPv4 on-demand (run DORA with a bounded wait, pump NIC RX during the command, then apply IP/mask/gw/dns).

# TODO (PORT Manager)
  [ ] Close all unused Ports
  [ ] Register open ports to the Application that Opens them
  [ ] Ports can only be opened by an internal Process

# TODO (Security Manager)
  [ ] Add ways to internally protect the data and what executes
  
# TODO (Application Sandbox/Playground)
  [ ] Create a protected execution space for all Applications



