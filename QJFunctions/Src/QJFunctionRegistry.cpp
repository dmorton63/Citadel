#include "QJFunctionRegistry.h"
#include "QJFJitAllocator.h"
#include "QCLogger.h"
#include "QFSVFS.h"
#include "QFSFile.h"
#include "QFSDirectory.h"

namespace QC
{
    namespace JFunc
    {
        namespace
        {
            static constexpr const char* LOG_MODULE = "QJFunctionRegistry";

            // Build a full path string: prefix + name into dst.
            static void buildPath(char* dst, QC::usize dstSize,
                                  const char* prefix, const char* name)
            {
                if (!dst || dstSize == 0) return;
                QC::usize plen = QC::String::strlen(prefix ? prefix : "");
                QC::usize nlen = QC::String::strlen(name   ? name   : "");
                if (plen + nlen + 1 > dstSize)
                {
                    dst[0] = '\0';
                    return;
                }
                for (QC::usize i = 0; i < plen; ++i) dst[i] = prefix[i];
                for (QC::usize i = 0; i < nlen; ++i) dst[plen + i] = name[i];
                dst[plen + nlen] = '\0';
            }

            // Returns true if 'name' ends with 'suffix'.
            static bool endsWith(const char* name, const char* suffix)
            {
                if (!name || !suffix) return false;
                QC::usize nlen = QC::String::strlen(name);
                QC::usize slen = QC::String::strlen(suffix);
                if (slen > nlen) return false;
                return QC::String::strcmp(name + nlen - slen, suffix) == 0;
            }

            // Returns true if 'haystack' contains 'needle' as a substring.
            static bool strContains(const char* haystack, const char* needle)
            {
                if (!haystack || !needle || !*needle) return false;
                const QC::usize nlen = QC::String::strlen(needle);
                const QC::usize hlen = QC::String::strlen(haystack);
                if (nlen > hlen) return false;
                for (QC::usize i = 0; i <= hlen - nlen; ++i)
                {
                    if (QC::String::memcmp(haystack + i, needle, nlen) == 0)
                        return true;
                }
                return false;
            }

            // Write a u8 to file.
            static bool writeU8(QFS::File* f, QC::u8 v)
            {
                return f->write(&v, 1) == 1;
            }

            // Write a u32 little-endian.
            static bool writeU32(QFS::File* f, QC::u32 v)
            {
                QC::u8 buf[4] = {
                    static_cast<QC::u8>(v & 0xFFu),
                    static_cast<QC::u8>((v >> 8) & 0xFFu),
                    static_cast<QC::u8>((v >> 16) & 0xFFu),
                    static_cast<QC::u8>((v >> 24) & 0xFFu)
                };
                return f->write(buf, 4) == 4;
            }

            // Write a u64 little-endian.
            static bool writeU64(QFS::File* f, QC::u64 v)
            {
                QC::u8 buf[8];
                for (QC::u32 i = 0; i < 8; ++i)
                    buf[i] = static_cast<QC::u8>((v >> (i * 8)) & 0xFFu);
                return f->write(buf, 8) == 8;
            }

            // Write a length-prefixed string (u32 len + bytes, no null terminator).
            static bool writeStr(QFS::File* f, const char* s)
            {
                QC::u32 len = static_cast<QC::u32>(QC::String::strlen(s ? s : ""));
                if (!writeU32(f, len)) return false;
                if (len == 0) return true;
                return f->write(s, len) == static_cast<QC::isize>(len);
            }

            // Read a u8.
            static bool readU8(QFS::File* f, QC::u8& v)
            {
                return f->read(&v, 1) == 1;
            }

            // Read a u32 little-endian.
            static bool readU32(QFS::File* f, QC::u32& v)
            {
                QC::u8 buf[4];
                if (f->read(buf, 4) != 4) return false;
                v = static_cast<QC::u32>(buf[0]) |
                    (static_cast<QC::u32>(buf[1]) << 8) |
                    (static_cast<QC::u32>(buf[2]) << 16) |
                    (static_cast<QC::u32>(buf[3]) << 24);
                return true;
            }

            // Read a u64 little-endian.
            static bool readU64(QFS::File* f, QC::u64& v)
            {
                QC::u8 buf[8];
                if (f->read(buf, 8) != 8) return false;
                v = 0;
                for (QC::u32 i = 0; i < 8; ++i)
                    v |= static_cast<QC::u64>(buf[i]) << (i * 8);
                return true;
            }

            // Read a length-prefixed string into a fixed buffer; dst is null-terminated.
            static bool readStr(QFS::File* f, char* dst, QC::usize dstSize)
            {
                QC::u32 len = 0;
                if (!readU32(f, len)) return false;
                if (len >= dstSize)
                {
                    // Skip bytes we cannot store.
                    QC::u8 discard;
                    for (QC::u32 i = 0; i < len; ++i)
                        f->read(&discard, 1);
                    if (dstSize > 0) dst[0] = '\0';
                    return false;
                }
                if (len > 0 && f->read(dst, len) != static_cast<QC::isize>(len))
                    return false;
                dst[len] = '\0';
                return true;
            }

        } // anonymous namespace

        // -----------------------------------------------------------------------
        // Singleton
        // -----------------------------------------------------------------------

        Registry& Registry::instance()
        {
            static Registry g;
            return g;
        }

        // -----------------------------------------------------------------------
        // Legacy API
        // -----------------------------------------------------------------------

        const Function* Registry::find(const char* name, QC::u32 version) const
        {
            if (!name) return nullptr;
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                const FunctionEntry& e = m_entries[i];
                if (e.fn.version == version &&
                    QC::String::strcmp(e.fn.name.c_str(), name) == 0)
                    return &e.fn;
            }
            return nullptr;
        }

        const Function* Registry::findByStableIdentity(const char* stableIdentity) const
        {
            if (!stableIdentity) return nullptr;
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                const FunctionEntry& e = m_entries[i];
                if (QC::String::strcmp(e.fn.stableIdentity.c_str(), stableIdentity) == 0)
                    return &e.fn;
            }
            return nullptr;
        }

        bool Registry::registerFunction(Function&& fn, const char* jsonPath)
        {
            if (findByStableIdentity(fn.stableIdentity.c_str()) ||
                find(fn.name.c_str(), fn.version))
                return false;

            FunctionEntry e;
            e.fn       = static_cast<Function&&>(fn);
            e.jsonPath = QC::String(jsonPath ? jsonPath : "");
            m_entries.push_back(static_cast<FunctionEntry&&>(e));
            return true;
        }

        // -----------------------------------------------------------------------
        // Lifecycle API
        // -----------------------------------------------------------------------

        FunctionEntry* Registry::findEntry(const char* stableIdentity)
        {
            if (!stableIdentity) return nullptr;
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                if (QC::String::strcmp(m_entries[i].fn.stableIdentity.c_str(),
                                       stableIdentity) == 0)
                    return &m_entries[i];
            }
            return nullptr;
        }

        const FunctionEntry* Registry::findEntry(const char* stableIdentity) const
        {
            if (!stableIdentity) return nullptr;
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                if (QC::String::strcmp(m_entries[i].fn.stableIdentity.c_str(),
                                       stableIdentity) == 0)
                    return &m_entries[i];
            }
            return nullptr;
        }

        bool Registry::validate(const char* stableIdentity, SCContext* sc)
        {
            FunctionEntry* e = findEntry(stableIdentity);
            if (!e)
            {
                QC_LOG_WARN(LOG_MODULE, "validate: not found identity=%s", stableIdentity);
                return false;
            }

            // Compute content hash over the raw JSON of the function.
            // We hash the signature bytes (canonical function shape) as the content hash.
            QC::u8 digest[Engine::HASH_BYTES];
            QC::String hexStr;
            Error err;
            if (!Engine::computeSignatureHash(e->fn, digest, hexStr, err))
            {
                QC_LOG_WARN(LOG_MODULE, "validate: hash failed identity=%s code=%u",
                            stableIdentity, static_cast<QC::u32>(err.code));
                return false;
            }

            // If SC is available, verify the function's signature field against trust store.
            if (sc && e->fn.signatureHashHex.c_str() && *e->fn.signatureHashHex.c_str())
            {
                // The signature hash is a self-signed content digest for DEBUG functions.
                // For PRODUCTION, this would call into the trust store.
                // v1: skip for DEBUG mode (Engine::validate already enforces E_AUTH
                // for PRODUCTION modules at parse time).
            }

            QC::String::memcpy(e->contentHash, digest, Engine::HASH_BYTES);
            e->state = FunctionState::Validated;
            e->lastValidatedTick = sc ? sc->currentTick() : 0u;

            if (m_jitDebugMode)
            {
                QC_LOG_INFO(LOG_MODULE, "jit_debug: validate identity=%s jitAllowed=%u state=%u",
                            stableIdentity,
                            e->jitAllowed ? 1u : 0u,
                            static_cast<QC::u32>(e->state));
            }

            if (sc)
                sc->auditLog("fn_validated", stableIdentity);

            QC_LOG_INFO(LOG_MODULE, "validate: ok identity=%s sig=%s",
                        stableIdentity, hexStr.c_str());
            return true;
        }

        bool Registry::markJitReady(const char* stableIdentity, SCContext* sc)
        {
            if (!sc)
            {
                QC_LOG_WARN(LOG_MODULE, "markJitReady: sc required");
                return false;
            }

            FunctionEntry* e = findEntry(stableIdentity);
            if (!e)
            {
                QC_LOG_WARN(LOG_MODULE, "markJitReady: not found identity=%s", stableIdentity);
                return false;
            }

            if (e->state != FunctionState::Validated)
            {
                QC_LOG_WARN(LOG_MODULE, "markJitReady: must be Validated identity=%s state=%u",
                            stableIdentity, static_cast<QC::u32>(e->state));
                return false;
            }

            if (!e->jitAllowed)
            {
                QC_LOG_WARN(LOG_MODULE, "markJitReady: JIT not allowed identity=%s", stableIdentity);
                return false;
            }

            if (sc->deviceState() != DeviceState::Operational)
            {
                QC_LOG_WARN(LOG_MODULE, "markJitReady: device not Operational identity=%s", stableIdentity);
                return false;
            }

            e->state = FunctionState::JitReady;

            if (m_jitDebugMode)
            {
                QC_LOG_INFO(LOG_MODULE, "jit_debug: ready identity=%s requiredAuthority=%u",
                            stableIdentity,
                            static_cast<QC::u32>(e->requiredAuthority));
            }

            sc->auditLog("fn_jit_ready", stableIdentity);
            QC_LOG_INFO(LOG_MODULE, "markJitReady: ok identity=%s", stableIdentity);
            return true;
        }

        bool Registry::markDllOverride(const char*  stableIdentity,
                                       SCContext*   sc,
                                       const char*  dllPath,
                                       DllCallFn    callFn)
        {
            if (!sc)
            {
                QC_LOG_WARN(LOG_MODULE, "markDllOverride: sc required");
                return false;
            }

            FunctionEntry* e = findEntry(stableIdentity);
            if (!e)
            {
                QC_LOG_WARN(LOG_MODULE, "markDllOverride: not found identity=%s", stableIdentity);
                return false;
            }

            // v1: DLL signature verification deferred to DLL loader implementation.
            // When the DLL loader exists, call sc->verifySignature() on the DLL hash.

            e->dllPath       = QC::String(dllPath ? dllPath : "");
            e->dllCallFn     = callFn;
            e->hasDllOverride = true;
            e->kind          = FunctionKind::DllOverride;
            e->state         = FunctionState::DllOverride;

            if (m_jitDebugMode)
            {
                QC_LOG_INFO(LOG_MODULE, "jit_debug: dll override identity=%s dll=%s",
                            stableIdentity,
                            dllPath ? dllPath : "(stub)");
            }

            sc->auditLog("fn_dll_override", stableIdentity);
            QC_LOG_INFO(LOG_MODULE, "markDllOverride: ok identity=%s dll=%s",
                        stableIdentity, dllPath ? dllPath : "(stub)");
            return true;
        }

        void Registry::invalidate(const char*      stableIdentity,
                                  InvalidateReason reason,
                                  SCContext*        sc)
        {
            FunctionEntry* e = findEntry(stableIdentity);
            if (!e) return;

            // Free any JIT pages.
            if (e->state == FunctionState::JitCompiled && e->dllCallFn == nullptr)
            {
                // JitAllocator tracks by pointer; we don't store the jit ptr on FunctionEntry
                // in v1 (codegen not yet wired), so nothing to free here.
            }

            e->state      = FunctionState::Unvalidated;
            e->dllCallFn  = nullptr;

            if (m_jitDebugMode)
            {
                QC_LOG_INFO(LOG_MODULE, "jit_debug: invalidate identity=%s reason=%u",
                            stableIdentity,
                            static_cast<QC::u32>(reason));
            }

            if (sc)
                sc->auditLog("fn_invalidated", stableIdentity);

            QC_LOG_INFO(LOG_MODULE, "invalidate: identity=%s reason=%u",
                        stableIdentity, static_cast<QC::u32>(reason));
        }

        void Registry::invalidateAll(InvalidateReason reason, SCContext* sc)
        {
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                FunctionEntry& e = m_entries[i];
                e.state     = FunctionState::Unvalidated;
                e.dllCallFn = nullptr;
            }
            // Free all JIT pages.
            JitAllocator::instance().freeAll();

            if (m_jitDebugMode)
                QC_LOG_INFO(LOG_MODULE, "jit_debug: invalidate_all reason=%u", static_cast<QC::u32>(reason));

            if (sc)
                sc->auditLog("fn_registry_invalidated_all", "reason");

            QC_LOG_INFO(LOG_MODULE, "invalidateAll: all entries reset reason=%u",
                        static_cast<QC::u32>(reason));
        }

        // -----------------------------------------------------------------------
        // VFS boot scanner
        // -----------------------------------------------------------------------

        void Registry::scanJsonFunctions(QFS::VFS& vfs)
        {
            static constexpr const char* DIR_PATH = "/system/fn";
            static constexpr const char* SUFFIX   = ".fn.json";

            QFS::Directory* dir = vfs.openDir(DIR_PATH);
            if (!dir || !dir->isOpen())
            {
                QC_LOG_INFO(LOG_MODULE, "scanJsonFunctions: dir not found or empty (%s)", DIR_PATH);
                return;
            }

            QC::u32 added    = 0;
            QC::u32 updated  = 0;
            QC::u32 failed   = 0;

            QFS::DirEntry de;
            while (dir->read(&de))
            {
                if (de.type != QFS::FileType::Regular) continue;
                if (!endsWith(de.name, SUFFIX))        continue;

                char path[512];
                buildPath(path, sizeof(path), "/system/fn/", de.name);
                if (!path[0]) { ++failed; continue; }

                Function fn;
                Error    err;
                if (!Engine::loadFromVfsPath(path, fn, err))
                {
                    QC_LOG_WARN(LOG_MODULE, "scanJsonFunctions: parse failed path=%s code=%u",
                                path, static_cast<QC::u32>(err.code));
                    ++failed;
                    continue;
                }

                FunctionEntry* existing = findEntry(fn.stableIdentity.c_str());
                if (existing)
                {
                    // Refresh JSON path; if content may have changed, reset to Unvalidated.
                    existing->jsonPath = QC::String(path);
                    existing->state    = FunctionState::Unvalidated;
                    ++updated;
                }
                else
                {
                    FunctionEntry e;
                    e.fn       = static_cast<Function&&>(fn);
                    e.jsonPath = QC::String(path);
                    e.state    = FunctionState::Unvalidated;
                    m_entries.push_back(static_cast<FunctionEntry&&>(e));
                    ++added;
                }
            }

            vfs.closeDir(dir);

            QC_LOG_INFO(LOG_MODULE,
                        "scanJsonFunctions: added=%u updated=%u failed=%u total=%u",
                        added, updated, failed, static_cast<QC::u32>(m_entries.size()));
        }

        void Registry::scanDllModules(QFS::VFS& vfs)
        {
            static constexpr const char* DIR_PATH = "/system/modules";
            static constexpr const char* SUFFIX   = ".dll";

            QFS::Directory* dir = vfs.openDir(DIR_PATH);
            if (!dir || !dir->isOpen())
            {
                QC_LOG_INFO(LOG_MODULE, "scanDllModules: dir not found or empty (%s)", DIR_PATH);
                return;
            }

            QC::u32 noted = 0;

            QFS::DirEntry de;
            while (dir->read(&de))
            {
                if (de.type != QFS::FileType::Regular) continue;
                if (!endsWith(de.name, SUFFIX))        continue;

                char path[512];
                buildPath(path, sizeof(path), "/system/modules/", de.name);
                if (!path[0]) continue;

                // v1: note the DLL path on any entry whose stableIdentity stem
                // matches the file name without the .dll suffix.
                // Full DLL metadata parsing (override declarations) is deferred
                // until the DLL loader is implemented.
                // For now, flag hasDllOverride = true and record the path.
                for (QC::usize i = 0; i < m_entries.size(); ++i)
                {
                    FunctionEntry& e = m_entries[i];
                    if (e.hasDllOverride) continue; // already claimed
                    // Simple heuristic: match by function name embedded in DLL filename.
                    if (strContains(de.name, e.fn.name.c_str()))
                    {
                        e.dllPath       = QC::String(path);
                        e.hasDllOverride = true;
                        QC_LOG_INFO(LOG_MODULE, "scanDllModules: noted dll=%s for identity=%s",
                                    path, e.fn.stableIdentity.c_str());
                        ++noted;
                    }
                }
            }

            vfs.closeDir(dir);
            QC_LOG_INFO(LOG_MODULE, "scanDllModules: noted=%u", noted);
        }

        // -----------------------------------------------------------------------
        // Snapshot persistence
        //
        // Binary format (FNREG v1):
        //   [4]  magic: 'F','N','R','G'
        //   [4]  format version: 1 (u32 LE)
        //   [4]  entry count (u32 LE)
        //   For each entry:
        //     stableIdentity  (u32-len-prefixed string)
        //     jsonPath        (u32-len-prefixed string)
        //     dllPath         (u32-len-prefixed string)
        //     state           (u8 — FunctionState)
        //     kind            (u8 — FunctionKind)
        //     requiredAuthority (u8)
        //     contentHash[32] (32 bytes)
        //     jitAllowed      (u8: 0/1)
        //     hasDllOverride  (u8: 0/1)
        //     lastValidatedTick (u64 LE)
        // -----------------------------------------------------------------------

        bool Registry::saveSnapshot(QFS::VFS& vfs, const char* path) const
        {
            if (!path) return false;

            QFS::File* f = vfs.open(path,
                QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f || !f->isOpen())
            {
                QC_LOG_WARN(LOG_MODULE, "saveSnapshot: cannot open %s", path);
                return false;
            }

            bool ok = true;

            // Magic + version.
            static const QC::u8 magic[4] = { 'F', 'N', 'R', 'G' };
            ok = ok && (f->write(magic, 4) == 4);
            ok = ok && writeU32(f, 1u); // format version
            ok = ok && writeU32(f, static_cast<QC::u32>(m_entries.size()));

            for (QC::usize i = 0; i < m_entries.size() && ok; ++i)
            {
                const FunctionEntry& e = m_entries[i];
                ok = ok && writeStr(f, e.fn.stableIdentity.c_str());
                ok = ok && writeStr(f, e.jsonPath.c_str());
                ok = ok && writeStr(f, e.dllPath.c_str());
                ok = ok && writeU8(f, static_cast<QC::u8>(e.state));
                ok = ok && writeU8(f, static_cast<QC::u8>(e.kind));
                ok = ok && writeU8(f, static_cast<QC::u8>(e.requiredAuthority));
                ok = ok && (f->write(e.contentHash, Engine::HASH_BYTES) == static_cast<QC::isize>(Engine::HASH_BYTES));
                ok = ok && writeU8(f, e.jitAllowed    ? 1u : 0u);
                ok = ok && writeU8(f, e.hasDllOverride ? 1u : 0u);
                ok = ok && writeU64(f, e.lastValidatedTick);
            }

            f->flush();
            vfs.close(f);

            if (ok)
                QC_LOG_INFO(LOG_MODULE, "saveSnapshot: wrote %u entries to %s",
                            static_cast<QC::u32>(m_entries.size()), path);
            else
                QC_LOG_WARN(LOG_MODULE, "saveSnapshot: write error path=%s", path);

            return ok;
        }

        bool Registry::loadSnapshot(QFS::VFS& vfs, const char* path)
        {
            if (!path) return false;

            QFS::File* f = vfs.open(path, QFS::OpenMode::Read);
            if (!f || !f->isOpen())
            {
                QC_LOG_INFO(LOG_MODULE, "loadSnapshot: not found %s (first boot)", path);
                return false;
            }

            bool ok = true;

            // Validate magic.
            QC::u8 magic[4] = {};
            ok = ok && (f->read(magic, 4) == 4);
            if (!ok || magic[0] != 'F' || magic[1] != 'N' ||
                         magic[2] != 'R' || magic[3] != 'G')
            {
                QC_LOG_WARN(LOG_MODULE, "loadSnapshot: bad magic path=%s", path);
                vfs.close(f);
                return false;
            }

            QC::u32 version = 0;
            ok = ok && readU32(f, version);
            if (!ok || version != 1u)
            {
                QC_LOG_WARN(LOG_MODULE, "loadSnapshot: unsupported version=%u", version);
                vfs.close(f);
                return false;
            }

            QC::u32 count = 0;
            ok = ok && readU32(f, count);

            static constexpr QC::usize STR_BUF = 512;
            char identBuf[STR_BUF];
            char jsonBuf[STR_BUF];
            char dllBuf[STR_BUF];

            for (QC::u32 i = 0; i < count && ok; ++i)
            {
                ok = ok && readStr(f, identBuf, STR_BUF);
                ok = ok && readStr(f, jsonBuf,  STR_BUF);
                ok = ok && readStr(f, dllBuf,   STR_BUF);

                QC::u8 stateByte = 0, kindByte = 0, authByte = 0;
                ok = ok && readU8(f, stateByte);
                ok = ok && readU8(f, kindByte);
                ok = ok && readU8(f, authByte);

                QC::u8 hash[Engine::HASH_BYTES] = {};
                ok = ok && (f->read(hash, Engine::HASH_BYTES) == static_cast<QC::isize>(Engine::HASH_BYTES));

                QC::u8 jitFlag = 0, dllFlag = 0;
                ok = ok && readU8(f, jitFlag);
                ok = ok && readU8(f, dllFlag);

                QC::u64 tick = 0;
                ok = ok && readU64(f, tick);

                if (!ok) break;

                // Apply to matching entry (function definition already scanned
                // from .fn.json; snapshot only updates lifecycle metadata).
                FunctionEntry* e = findEntry(identBuf);
                if (!e)
                {
                    // Entry not yet in registry (scan not run yet or function removed).
                    QC_LOG_INFO(LOG_MODULE, "loadSnapshot: no entry for identity=%s (skipped)", identBuf);
                    continue;
                }

                e->jsonPath          = QC::String(jsonBuf);
                e->dllPath           = QC::String(dllBuf);
                e->state             = static_cast<FunctionState>(stateByte);
                e->kind              = static_cast<FunctionKind>(kindByte);
                e->requiredAuthority = static_cast<AuthorityLevel>(authByte);
                QC::String::memcpy(e->contentHash, hash, Engine::HASH_BYTES);
                e->jitAllowed        = (jitFlag != 0);
                e->hasDllOverride    = (dllFlag != 0);
                e->lastValidatedTick = tick;
            }

            vfs.close(f);

            if (ok)
                QC_LOG_INFO(LOG_MODULE, "loadSnapshot: loaded %u entries from %s", count, path);
            else
                QC_LOG_WARN(LOG_MODULE, "loadSnapshot: read error path=%s", path);

            return ok;
        }

    } // namespace JFunc
} // namespace QC
