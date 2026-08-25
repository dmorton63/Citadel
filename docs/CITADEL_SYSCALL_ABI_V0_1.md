# Citadel Syscall ABI v0.1

## 1. Scope

This document defines the concrete syscall ABI for Citadel App Platform v0.1.

It is normative for:
- Kernel syscall dispatcher IDs
- Runtime syscall wrappers
- Native tooling and FFI bridges

Machine-readable registry:
- `docs/CITADEL_SYSCALL_ABI_V0_1.json` mirrors this document for code generation and test tooling.

Consistency check command:
- `python3 tools/check_syscall_registry_consistency.py`

## 2. Numbering Scheme

Syscall ID is 16-bit:
- High byte: family ID
- Low byte: operation ID

Formula:
- `sys_id = (family << 8) | op`

Examples:
- Process.Exit: family `0x01`, op `0x02`, sys_id `0x0102`
- File.Read: family `0x04`, op `0x03`, sys_id `0x0403`

## 3. Calling Convention

- Architecture: x86_64
- Register convention: SysV
- Return register: `rax`
- Success: `rax >= 0`
- Failure: `rax < 0` (negative platform error code)

Argument ABI:
- Up to 6 scalar/pointer args in registers
- Additional args via pointer to packed request struct

## 4. Common Types

- `u8`, `u16`, `u32`, `u64`
- `i32`, `i64`
- `usize` (u64 in v0.1)
- `handle_t` (u64)
- `pid_t` (u64)
- `tid_t` (u64)
- `time_ns_t` (u64)

Pointer and buffer conventions:
- User pointers must be canonical, readable/writable as required.
- Kernel validates user buffers and lengths before access.

## 5. Common Error Codes

All error values are negative.

- `-1` `PLT_E_UNKNOWN`
- `-2` `PLT_E_NOT_IMPLEMENTED`
- `-3` `PLT_E_INVALID_ARG`
- `-4` `PLT_E_BAD_HANDLE`
- `-5` `PLT_E_ACCESS_DENIED`
- `-6` `PLT_E_NOT_FOUND`
- `-7` `PLT_E_ALREADY_EXISTS`
- `-8` `PLT_E_TIMEOUT`
- `-9` `PLT_E_WOULD_BLOCK`
- `-10` `PLT_E_IO`
- `-11` `PLT_E_NO_MEMORY`
- `-12` `PLT_E_BAD_STATE`
- `-13` `PLT_E_TRUNCATED`
- `-14` `PLT_E_CAPABILITY_DENIED`
- `-15` `PLT_E_ABI_MISMATCH`

## 6. Family 0x01 Process

Required permission examples:
- `process.spawn` for spawn
- none for exit/self queries

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Process.Self | `0x0101` | `i64 sys_process_self()` | `pid_t` |
| Process.Exit | `0x0102` | `i64 sys_process_exit(i32 code)` | does not return on success |
| Process.Spawn | `0x0103` | `i64 sys_process_spawn(const SpawnReq* req, pid_t* out_pid)` | `0` |
| Process.Wait | `0x0104` | `i64 sys_process_wait(pid_t pid, u64 timeout_ns, i32* out_code)` | `0` |
| Process.Sleep | `0x0105` | `i64 sys_process_sleep(u64 duration_ns)` | `0` |

SpawnReq fields (packed):
- `const char* path`
- `const char* const* argv`
- `const char* const* envv`
- `u32 flags`
- `handle_t std_in`
- `handle_t std_out`
- `handle_t std_err`

## 7. Family 0x02 Thread

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Thread.Self | `0x0201` | `i64 sys_thread_self()` | `tid_t` |
| Thread.Create | `0x0202` | `i64 sys_thread_create(const ThreadCreateReq* req, tid_t* out_tid)` | `0` |
| Thread.Join | `0x0203` | `i64 sys_thread_join(tid_t tid, u64 timeout_ns, i32* out_code)` | `0` |
| Thread.Yield | `0x0204` | `i64 sys_thread_yield()` | `0` |

ThreadCreateReq fields:
- `u64 entry_fn`
- `u64 arg0`
- `u64 stack_size`
- `u32 flags`

## 8. Family 0x03 Memory

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Memory.Map | `0x0301` | `i64 sys_memory_map(const MemMapReq* req, u64* out_addr)` | `0` |
| Memory.Unmap | `0x0302` | `i64 sys_memory_unmap(u64 addr, u64 size)` | `0` |
| Memory.Protect | `0x0303` | `i64 sys_memory_protect(u64 addr, u64 size, u32 prot)` | `0` |
| Memory.MapShared | `0x0304` | `i64 sys_memory_map_shared(handle_t shm, u64 offset, u64 size, u32 prot, u64* out_addr)` | `0` |

MemMapReq fields:
- `u64 hint_addr`
- `u64 size`
- `u32 prot`
- `u32 flags`

## 9. Family 0x04 FileSystem

Required permissions:
- `fs.read`
- `fs.write`

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| File.Open | `0x0401` | `i64 sys_file_open(const char* path, u32 flags, u32 mode, handle_t* out_h)` | `0` |
| File.Close | `0x0402` | `i64 sys_file_close(handle_t h)` | `0` |
| File.Read | `0x0403` | `i64 sys_file_read(handle_t h, void* buf, u64 len, u64* out_n)` | `0` |
| File.Write | `0x0404` | `i64 sys_file_write(handle_t h, const void* buf, u64 len, u64* out_n)` | `0` |
| File.Seek | `0x0405` | `i64 sys_file_seek(handle_t h, i64 off, u32 whence, u64* out_pos)` | `0` |
| File.Stat | `0x0406` | `i64 sys_file_stat(const char* path, FileStat* out_stat)` | `0` |
| File.List | `0x0407` | `i64 sys_file_list(const char* path, DirListReq* req)` | `0` |
| File.Remove | `0x0408` | `i64 sys_file_remove(const char* path)` | `0` |
| File.Rename | `0x0409` | `i64 sys_file_rename(const char* oldp, const char* newp)` | `0` |

File.Open flag bits:
- `0x0001` read
- `0x0002` write
- `0x0004` create
- `0x0008` truncate
- `0x0010` append

## 10. Family 0x05 Time

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Time.MonotonicNs | `0x0501` | `i64 sys_time_monotonic_ns(u64* out_ns)` | `0` |
| Time.RealtimeNs | `0x0502` | `i64 sys_time_realtime_ns(u64* out_ns)` | `0` |
| Time.TimerCreate | `0x0503` | `i64 sys_time_timer_create(const TimerReq* req, handle_t* out_timer)` | `0` |
| Time.TimerWait | `0x0504` | `i64 sys_time_timer_wait(handle_t timer, u64 timeout_ns)` | `0` |

## 11. Family 0x06 IPC

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| IPC.ChannelCreate | `0x0601` | `i64 sys_ipc_channel_create(u32 flags, handle_t* out_rx, handle_t* out_tx)` | `0` |
| IPC.Send | `0x0602` | `i64 sys_ipc_send(handle_t tx, const void* buf, u64 len, u64 timeout_ns)` | `0` |
| IPC.Recv | `0x0603` | `i64 sys_ipc_recv(handle_t rx, void* buf, u64 cap, u64* out_n, u64 timeout_ns)` | `0` |

## 12. Family 0x07 Net

Required permissions:
- `net.client`
- `net.server`

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Net.Socket | `0x0701` | `i64 sys_net_socket(u32 domain, u32 type, u32 proto, handle_t* out_s)` | `0` |
| Net.Connect | `0x0702` | `i64 sys_net_connect(handle_t s, const SockAddr* addr, u32 len)` | `0` |
| Net.Bind | `0x0703` | `i64 sys_net_bind(handle_t s, const SockAddr* addr, u32 len)` | `0` |
| Net.Listen | `0x0704` | `i64 sys_net_listen(handle_t s, u32 backlog)` | `0` |
| Net.Accept | `0x0705` | `i64 sys_net_accept(handle_t s, SockAddr* out_addr, u32* inout_len, handle_t* out_c)` | `0` |
| Net.Send | `0x0706` | `i64 sys_net_send(handle_t s, const void* buf, u64 len, u32 flags, u64* out_n)` | `0` |
| Net.Recv | `0x0707` | `i64 sys_net_recv(handle_t s, void* buf, u64 cap, u32 flags, u64* out_n)` | `0` |
| Net.Close | `0x0708` | `i64 sys_net_close(handle_t s)` | `0` |

## 13. Family 0x08 UI

Required permission:
- `ui.window`

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| UI.WindowCreate | `0x0801` | `i64 sys_ui_window_create(const WindowCreateReq* req, handle_t* out_w)` | `0` |
| UI.WindowDestroy | `0x0802` | `i64 sys_ui_window_destroy(handle_t w)` | `0` |
| UI.WindowShow | `0x0803` | `i64 sys_ui_window_show(handle_t w, u32 show)` | `0` |
| UI.EventPoll | `0x0804` | `i64 sys_ui_event_poll(handle_t w, UiEvent* out_evt, u64 timeout_ns)` | `0` |
| UI.WindowSetTitle | `0x0805` | `i64 sys_ui_window_set_title(handle_t w, const char* title)` | `0` |

## 14. Family 0x09 Graphics

Required permission:
- `ui.window`

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Gfx.SurfaceCreate | `0x0901` | `i64 sys_gfx_surface_create(handle_t window, const SurfaceReq* req, handle_t* out_surf)` | `0` |
| Gfx.SurfacePresent | `0x0902` | `i64 sys_gfx_surface_present(handle_t surf)` | `0` |
| Gfx.UploadBuffer | `0x0903` | `i64 sys_gfx_upload_buffer(handle_t surf, const void* buf, u64 len, u32 usage)` | `0` |
| Gfx.Resize | `0x0904` | `i64 sys_gfx_resize(handle_t surf, u32 w, u32 h)` | `0` |

## 15. Family 0x0A Security

| Name | Sys ID | Signature | Returns |
|---|---:|---|---|
| Sec.CapQuery | `0x0A01` | `i64 sys_sec_cap_query(handle_t caps, const char* perm, u32* out_allowed)` | `0` |
| Sec.TokenInfo | `0x0A02` | `i64 sys_sec_token_info(handle_t caps, CapTokenInfo* out_info)` | `0` |
| Sec.AttestSelf | `0x0A03` | `i64 sys_sec_attest_self(AttestReq* req, AttestResp* out_resp)` | `0` |

## 16. Compatibility and Extension Rules

- Existing syscall IDs in v0.1 are immutable.
- New syscalls must use new op IDs; no renumbering.
- Reserved families for future use: `0x0B` to `0x1F`.
- Unsupported known syscall must return `PLT_E_NOT_IMPLEMENTED`.
- Unknown family or malformed request returns `PLT_E_INVALID_ARG` or `PLT_E_ABI_MISMATCH`.

## 17. Runtime Mapping Notes

CiteLang runtime wrappers should map high-level APIs to syscalls:
- `core.fs.*` -> `0x04xx`
- `core.time.*` -> `0x05xx`
- `core.net.*` -> `0x07xx`
- `core.ui.*` -> `0x08xx` and `0x09xx`
- `core.security.*` -> `0x0Axx`

Wrapper policy:
- Convert negative platform errors to typed runtime diagnostics.
- Preserve raw platform code for debugging telemetry.

Code stubs aligned to this ABI:
- `QKernel/Include/QKSyscallABI.h`
- `QKernel/Src/QKSyscallABI.cpp`
- `QJFunctions/Include/QJFCitadelSyscalls.h`
- `QJFunctions/Src/QJFCitadelSyscalls.cpp`

## 18. Initial Test Matrix

Required ABI tests:
- Validate dispatch for every defined sys_id.
- Validate permission denial paths (`PLT_E_CAPABILITY_DENIED`).
- Validate pointer safety checks for read/write buffers.
- Validate timeout behavior for wait/poll syscalls.
- Validate stable behavior across repeated calls and invalid handles.
