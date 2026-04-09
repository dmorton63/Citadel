#include "QKModuleLoader.h"

#include "QFSFile.h"
#include "QFSVFS.h"
#include "QKSecurityCenter.h"
#include "QKTime.h"
#include "QCSha256.h"
#include "QCString.h"

namespace QK::Module
{
    namespace
    {
        struct ModuleEntry
        {
            char id[32] = {0};
            char path[192] = {0};
            QC::u8 depCount = 0;
            char deps[8][32] = {{0}};
            char expectedHashHex[65] = {0};
            char signature[96] = {0}; // v1:<64hex>
            char keyId[32] = {0};
            char tags[4][24] = {{0}};
            QC::u8 tagCount = 0;
            char stage[24] = {0};
            QC::u64 scheduledAtMs = 0;
            bool directExecAllowed = true;
            char namespaceId[48] = {0};
            QC::u16 versionMajor = 1;
            QC::u16 versionMinor = 0;
            bool internalOnly = false;
            bool fetched = false;
            bool sandboxedFirstRun = false;
            QC::u64 bytes = 0;
        };

        struct CatalogState
        {
            bool loaded = false;
            char path[192] = {0};
            QC::Vector<ModuleEntry> entries;
        };

        static CatalogState g_catalog;
        static InspectionState g_lastInspection;

        static bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        static void trimInPlace(char *s)
        {
            if (!s)
                return;
            QC::usize len = QC::String::strlen(s);
            QC::usize start = 0;
            while (start < len && isSpace(s[start]))
                ++start;
            QC::usize end = len;
            while (end > start && isSpace(s[end - 1]))
                --end;
            QC::usize out = 0;
            for (QC::usize i = start; i < end; ++i)
                s[out++] = s[i];
            s[out] = '\0';
        }

        static bool readToken(const char *&p, char *out, QC::usize cap)
        {
            if (!out || cap == 0)
                return false;
            out[0] = '\0';
            if (!p)
                return false;
            while (*p && isSpace(*p))
                ++p;
            if (*p == '\0')
                return false;
            QC::usize i = 0;
            while (*p && !isSpace(*p) && i + 1 < cap)
                out[i++] = *p++;
            out[i] = '\0';
            return i > 0;
        }

        static bool readAll(const char *path, QC::Vector<char> &out)
        {
            out.clear();
            QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!f)
                return false;
            const QC::isize szSigned = f->size();
            if (szSigned <= 0)
            {
                QFS::VFS::instance().close(f);
                return false;
            }
            const QC::usize sz = static_cast<QC::usize>(szSigned);
            out.resize(sz + 1);
            QC::usize off = 0;
            while (off < sz)
            {
                const QC::isize n = f->read(out.data() + off, sz - off);
                if (n <= 0)
                    break;
                off += static_cast<QC::usize>(n);
            }
            QFS::VFS::instance().close(f);
            if (off != sz)
            {
                out.clear();
                return false;
            }
            out[sz] = '\0';
            return true;
        }

        static int findModule(const char *id)
        {
            for (QC::usize i = 0; i < g_catalog.entries.size(); ++i)
            {
                if (QC::String::strcmp(g_catalog.entries[i].id, id) == 0)
                    return static_cast<int>(i);
            }
            return -1;
        }

        static bool isLowerHex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }

        static bool isLowerHexString(const char *s, QC::usize exactLen)
        {
            if (!s)
                return false;
            const QC::usize n = QC::String::strlen(s);
            if (n != exactLen)
                return false;
            for (QC::usize i = 0; i < n; ++i)
            {
                if (!isLowerHex(s[i]))
                    return false;
            }
            return true;
        }

        static bool parsePrefixedValue(const char *tok, const char *prefix, char *out, QC::usize outCap)
        {
            if (!tok || !prefix || !out || outCap == 0)
                return false;
            const QC::usize pLen = QC::String::strlen(prefix);
            if (QC::String::memcmp(tok, prefix, pLen) != 0)
                return false;
            QC::String::memset(out, 0, outCap);
            QC::String::strncpy(out, tok + pLen, outCap - 1);
            return true;
        }

        static bool startsWith(const char *s, const char *prefix)
        {
            if (!s || !prefix)
                return false;
            const QC::usize n = QC::String::strlen(prefix);
            return QC::String::memcmp(s, prefix, n) == 0;
        }

        static bool parseU64(const char *s, QC::u64 &out)
        {
            if (!s || !*s)
                return false;
            QC::u64 v = 0;
            for (const char *p = s; *p; ++p)
            {
                if (*p < '0' || *p > '9')
                    return false;
                v = (v * 10ULL) + static_cast<QC::u64>(*p - '0');
            }
            out = v;
            return true;
        }

        static bool parseVersion(const char *s, QC::u16 &major, QC::u16 &minor)
        {
            if (!s || !*s)
                return false;
            QC::u64 a = 0;
            QC::u64 b = 0;
            const char *dot = nullptr;
            for (const char *p = s; *p; ++p)
            {
                if (*p == '.')
                {
                    dot = p;
                    break;
                }
                if (*p < '0' || *p > '9')
                    return false;
                a = (a * 10ULL) + static_cast<QC::u64>(*p - '0');
            }
            if (!dot)
            {
                major = static_cast<QC::u16>(a);
                minor = 0;
                return true;
            }
            for (const char *p = dot + 1; *p; ++p)
            {
                if (*p < '0' || *p > '9')
                    return false;
                b = (b * 10ULL) + static_cast<QC::u64>(*p - '0');
            }
            major = static_cast<QC::u16>(a);
            minor = static_cast<QC::u16>(b);
            return true;
        }

        static bool hasTag(const ModuleEntry &entry, const char *tag)
        {
            if (!tag)
                return false;
            for (QC::u8 i = 0; i < entry.tagCount; ++i)
            {
                if (QC::String::strcmp(entry.tags[i], tag) == 0)
                    return true;
            }
            return false;
        }

        static bool writeStagingArtifact(const ModuleEntry &entry, const QC::u8 *data, QC::usize size)
        {
            if (!data || size == 0)
                return false;
            if (QFS::VFS::instance().createDir("/system") != QC::Status::Success &&
                QFS::VFS::instance().createDir("/system") != QC::Status::Busy)
                return false;
            if (QFS::VFS::instance().createDir("/system/updates") != QC::Status::Success &&
                QFS::VFS::instance().createDir("/system/updates") != QC::Status::Busy)
                return false;
            if (QFS::VFS::instance().createDir("/system/updates/staging") != QC::Status::Success &&
                QFS::VFS::instance().createDir("/system/updates/staging") != QC::Status::Busy)
                return false;
            if (QFS::VFS::instance().createDir("/system/updates/staging/modules") != QC::Status::Success &&
                QFS::VFS::instance().createDir("/system/updates/staging/modules") != QC::Status::Busy)
                return false;

            char stagePath[224];
            QC::String::memset(stagePath, 0, sizeof(stagePath));
            QC::String::strncpy(stagePath, "/system/updates/staging/modules/", sizeof(stagePath) - 1);
            const QC::usize base = QC::String::strlen(stagePath);
            QC::String::strncpy(stagePath + base, entry.id, sizeof(stagePath) - base - 1);
            const QC::usize used = QC::String::strlen(stagePath);
            if (used + 5 < sizeof(stagePath))
                QC::String::strncpy(stagePath + used, ".pkg", sizeof(stagePath) - used - 1);

            QFS::File *f = QFS::VFS::instance().open(stagePath, QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
                return false;

            QC::usize off = 0;
            while (off < size)
            {
                const QC::isize n = f->write(data + off, size - off);
                if (n <= 0)
                {
                    QFS::VFS::instance().close(f);
                    return false;
                }
                off += static_cast<QC::usize>(n);
            }
            QFS::VFS::instance().close(f);
            return true;
        }

        static bool enforceNamespaceVersionPolicy(ModuleEntry &entry)
        {
            static constexpr QC::u16 kSupportedMajor = 1;
            if (!entry.namespaceId[0])
                QC::String::strncpy(entry.namespaceId, "citadel.public", sizeof(entry.namespaceId) - 1);

            if (startsWith(entry.namespaceId, "citadel.internal.sc"))
                entry.internalOnly = true;

            if (entry.versionMajor != kSupportedMajor)
            {
                QC::String::strncpy(g_lastInspection.detail, "module version rejected by namespace policy", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            if (entry.internalOnly && !startsWith(entry.id, "sc."))
            {
                QC::String::strncpy(g_lastInspection.detail, "internal namespace reserved for sc.*", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            return true;
        }

        static bool verifySignatureScaffold(const ModuleEntry &entry, const char digestHex[65])
        {
            if (!entry.expectedHashHex[0] || !isLowerHexString(entry.expectedHashHex, 64))
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: missing hash metadata", sizeof(g_lastInspection.detail) - 1);
                return false;
            }
            if (QC::String::strcmp(entry.expectedHashHex, digestHex) != 0)
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: payload hash mismatch", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            if (!entry.signature[0] || !entry.keyId[0])
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: missing sig/key metadata", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            const char *prefix = "v1:";
            const QC::usize prefixLen = 3;
            if (QC::String::memcmp(entry.signature, prefix, prefixLen) != 0)
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: unsupported signature format", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            const char *sigHex = entry.signature + prefixLen;
            if (!isLowerHexString(sigHex, 64))
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: malformed signature hex", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            char preimage[128];
            QC::String::memset(preimage, 0, sizeof(preimage));
            QC::String::strncpy(preimage, entry.keyId, sizeof(preimage) - 1);
            const QC::usize used = QC::String::strlen(preimage);
            if (used + 1 < sizeof(preimage))
                QC::String::strncpy(preimage + used, ":", sizeof(preimage) - used - 1);
            const QC::usize used2 = QC::String::strlen(preimage);
            if (used2 + 1 < sizeof(preimage))
                QC::String::strncpy(preimage + used2, digestHex, sizeof(preimage) - used2 - 1);

            QC::u8 sigDigest[32];
            char sigExpected[65];
            QC::String::memset(sigDigest, 0, sizeof(sigDigest));
            QC::String::memset(sigExpected, 0, sizeof(sigExpected));
            QC::Sha256(reinterpret_cast<const QC::u8 *>(preimage), QC::String::strlen(preimage), sigDigest);
            (void)QC::Sha256DigestToLowerHex(sigDigest, sigExpected, sizeof(sigExpected));

            if (QC::String::strcmp(sigExpected, sigHex) != 0)
            {
                QC::String::strncpy(g_lastInspection.detail, "signature verify: invalid signature", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            return true;
        }

        static bool fetchBinary(ModuleEntry &entry)
        {
            if (entry.fetched)
                return true;

            g_lastInspection = InspectionState{};

            QFS::File *f = QFS::VFS::instance().open(entry.path, QFS::OpenMode::Read);
            if (!f)
            {
                QC::String::strncpy(g_lastInspection.detail, "module payload missing", sizeof(g_lastInspection.detail) - 1);
                return false;
            }
            const QC::isize szSigned = f->size();
            if (szSigned <= 0)
            {
                QFS::VFS::instance().close(f);
                QC::String::strncpy(g_lastInspection.detail, "module payload empty", sizeof(g_lastInspection.detail) - 1);
                return false;
            }
            const QC::usize sz = static_cast<QC::usize>(szSigned);
            QC::Vector<QC::u8> tmp;
            tmp.resize(sz);
            QC::usize off = 0;
            while (off < sz)
            {
                const QC::isize n = f->read(tmp.data() + off, sz - off);
                if (n <= 0)
                    break;
                off += static_cast<QC::usize>(n);
            }
            QFS::VFS::instance().close(f);
            if (off != sz)
            {
                QC::String::strncpy(g_lastInspection.detail, "module payload read failed", sizeof(g_lastInspection.detail) - 1);
                return false;
            }

            if (!enforceNamespaceVersionPolicy(entry))
                return false;

            if (hasTag(entry, "downloaded") || hasTag(entry, "browser") || hasTag(entry, "update"))
                entry.directExecAllowed = false;

            if (entry.stage[0] == '\0')
                QC::String::strncpy(entry.stage, "downloaded", sizeof(entry.stage) - 1);

            if (QC::String::strcmp(entry.stage, "downloaded") == 0)
                (void)writeStagingArtifact(entry, tmp.data(), tmp.size());

            QC::u8 digest[32];
            char digestHex[65];
            QC::String::memset(digest, 0, sizeof(digest));
            QC::String::memset(digestHex, 0, sizeof(digestHex));
            QC::Sha256(tmp.data(), tmp.size(), digest);
            (void)QC::Sha256DigestToLowerHex(digest, digestHex, sizeof(digestHex));
            if (!verifySignatureScaffold(entry, digestHex))
            {
                char quarantinePath[192];
                QC::String::memset(quarantinePath, 0, sizeof(quarantinePath));
                if (QK::SecurityCenter::instance().quarantinePayload(entry.id, tmp.data(), tmp.size(), quarantinePath) == QC::Status::Success)
                    QC::String::strncpy(g_lastInspection.quarantinePath, quarantinePath, sizeof(g_lastInspection.quarantinePath) - 1);
                return false;
            }

            QK::SecurityCenter::PayloadScanResult scan{};
            const QC::Status scanSt = QK::SecurityCenter::instance().scanPayload(entry.path, tmp.data(), tmp.size(), scan);
            QC::String::strncpy(g_lastInspection.detail, scan.detail, sizeof(g_lastInspection.detail) - 1);
            if (scanSt != QC::Status::Success)
            {
                char quarantinePath[192];
                QC::String::memset(quarantinePath, 0, sizeof(quarantinePath));
                if (QK::SecurityCenter::instance().quarantinePayload(entry.id, tmp.data(), tmp.size(), quarantinePath) == QC::Status::Success)
                    QC::String::strncpy(g_lastInspection.quarantinePath, quarantinePath, sizeof(g_lastInspection.quarantinePath) - 1);
                return false;
            }

            char parkedPath[192];
            QC::String::memset(parkedPath, 0, sizeof(parkedPath));
            if (QK::SecurityCenter::instance().parkVerifiedUpdate(entry.id, tmp.data(), tmp.size(), parkedPath) == QC::Status::Success)
                QC::String::strncpy(g_lastInspection.parkedPath, parkedPath, sizeof(g_lastInspection.parkedPath) - 1);
            g_lastInspection.allowed = true;
            QC::String::strncpy(entry.stage, "verified", sizeof(entry.stage) - 1);

            entry.bytes = sz;
            entry.fetched = true;
            return true;
        }

        static bool fetchRec(int index, bool *visiting, bool *visited, FetchReport *out)
        {
            if (index < 0 || static_cast<QC::usize>(index) >= g_catalog.entries.size())
                return false;
            if (visiting[index])
                return false;
            if (visited[index])
                return true;

            visiting[index] = true;
            ModuleEntry &e = g_catalog.entries[static_cast<QC::usize>(index)];
            for (QC::u8 i = 0; i < e.depCount; ++i)
            {
                const int dep = findModule(e.deps[i]);
                if (dep < 0)
                    return false;
                if (!fetchRec(dep, visiting, visited, out))
                    return false;
            }

            if (!fetchBinary(e))
            {
                if (out && g_lastInspection.quarantinePath[0])
                    ++out->quarantinedModules;
                return false;
            }
            if (out)
            {
                ++out->loadedModules;
                out->loadedBytes += e.bytes;
                if (g_lastInspection.parkedPath[0])
                    ++out->parkedModules;
            }

            visiting[index] = false;
            visited[index] = true;
            return true;
        }

        static bool markSandboxedRec(int index, bool *seen)
        {
            if (index < 0 || static_cast<QC::usize>(index) >= g_catalog.entries.size())
                return false;
            if (seen[index])
                return true;
            seen[index] = true;

            ModuleEntry &e = g_catalog.entries[static_cast<QC::usize>(index)];
            e.sandboxedFirstRun = true;
            for (QC::u8 i = 0; i < e.depCount; ++i)
            {
                const int dep = findModule(e.deps[i]);
                if (dep < 0)
                    return false;
                if (!markSandboxedRec(dep, seen))
                    return false;
            }
            return true;
        }

        static bool allSandboxedRec(int index, bool *seen)
        {
            if (index < 0 || static_cast<QC::usize>(index) >= g_catalog.entries.size())
                return false;
            if (seen[index])
                return true;
            seen[index] = true;

            const ModuleEntry &e = g_catalog.entries[static_cast<QC::usize>(index)];
            if (!e.sandboxedFirstRun)
                return false;
            for (QC::u8 i = 0; i < e.depCount; ++i)
            {
                const int dep = findModule(e.deps[i]);
                if (dep < 0)
                    return false;
                if (!allSandboxedRec(dep, seen))
                    return false;
            }
            return true;
        }

        static QC::Status enforceSafeCutoverBoundary()
        {
            QC::u32 activeTasks = 0;
            const QC::Status st = QK::SecurityCenter::instance().waitForRotationBoundary(250, &activeTasks);
            if (st != QC::Status::Success)
                QC::String::strncpy(g_lastInspection.detail, "safe cutover deferred (busy)", sizeof(g_lastInspection.detail) - 1);
            return st;
        }

        static bool hasLoadedDependents(int index)
        {
            if (index < 0 || static_cast<QC::usize>(index) >= g_catalog.entries.size())
                return false;

            const ModuleEntry &target = g_catalog.entries[static_cast<QC::usize>(index)];
            for (QC::usize i = 0; i < g_catalog.entries.size(); ++i)
            {
                if (static_cast<int>(i) == index)
                    continue;
                const ModuleEntry &e = g_catalog.entries[i];
                if (!e.fetched)
                    continue;
                for (QC::u8 d = 0; d < e.depCount; ++d)
                {
                    if (QC::String::strcmp(e.deps[d], target.id) == 0)
                        return true;
                }
            }
            return false;
        }

        static void graphRec(int index, bool *seen, DependencyEdge *out, QC::usize cap, QC::usize &count)
        {
            if (index < 0 || static_cast<QC::usize>(index) >= g_catalog.entries.size())
                return;
            if (seen[index])
                return;
            seen[index] = true;

            const ModuleEntry &e = g_catalog.entries[static_cast<QC::usize>(index)];
            for (QC::u8 i = 0; i < e.depCount; ++i)
            {
                const int dep = findModule(e.deps[i]);
                if (dep >= 0 && count < cap)
                {
                    QC::String::strncpy(out[count].from, e.id, sizeof(out[count].from) - 1);
                    out[count].from[sizeof(out[count].from) - 1] = '\0';
                    QC::String::strncpy(out[count].to, e.deps[i], sizeof(out[count].to) - 1);
                    out[count].to[sizeof(out[count].to) - 1] = '\0';
                    ++count;
                }
                if (dep >= 0)
                    graphRec(dep, seen, out, cap, count);
            }
        }
    }

    Loader &Loader::instance()
    {
        static Loader l;
        return l;
    }

    QC::Status Loader::loadCatalog(const char *catalogPath)
    {
        if (!catalogPath || *catalogPath == '\0')
            return QC::Status::InvalidParam;

        if (g_catalog.loaded && QC::String::strcmp(g_catalog.path, catalogPath) == 0)
            return QC::Status::Success;

        QC::Vector<char> content;
        if (!readAll(catalogPath, content))
            return QC::Status::NotFound;

        g_catalog.entries.clear();
        QC::String::memset(g_catalog.path, 0, sizeof(g_catalog.path));
        QC::String::strncpy(g_catalog.path, catalogPath, sizeof(g_catalog.path) - 1);

        char line[320];
        QC::String::memset(line, 0, sizeof(line));
        QC::usize li = 0;

        auto parseLine = [&](char *raw)
        {
            trimInPlace(raw);
            if (raw[0] == '\0' || raw[0] == '#')
                return;

            const char *p = raw;
            ModuleEntry e;
            if (!readToken(p, e.id, sizeof(e.id)))
                return;
            if (!readToken(p, e.path, sizeof(e.path)))
                return;
            while (e.depCount < 8)
            {
                char dep[32];
                if (!readToken(p, dep, sizeof(dep)))
                    break;

                char value[96];
                QC::String::memset(value, 0, sizeof(value));
                if (parsePrefixedValue(dep, "hash=", value, sizeof(value)))
                {
                    QC::String::strncpy(e.expectedHashHex, value, sizeof(e.expectedHashHex) - 1);
                    continue;
                }
                if (parsePrefixedValue(dep, "sig=", value, sizeof(value)))
                {
                    QC::String::strncpy(e.signature, value, sizeof(e.signature) - 1);
                    continue;
                }
                if (parsePrefixedValue(dep, "key=", value, sizeof(value)))
                {
                    QC::String::strncpy(e.keyId, value, sizeof(e.keyId) - 1);
                    continue;
                }
                if (parsePrefixedValue(dep, "tag=", value, sizeof(value)))
                {
                    if (e.tagCount < 4)
                    {
                        QC::String::strncpy(e.tags[e.tagCount], value, sizeof(e.tags[e.tagCount]) - 1);
                        ++e.tagCount;
                    }
                    continue;
                }
                if (parsePrefixedValue(dep, "stage=", value, sizeof(value)))
                {
                    QC::String::strncpy(e.stage, value, sizeof(e.stage) - 1);
                    continue;
                }
                if (parsePrefixedValue(dep, "apply_ms=", value, sizeof(value)))
                {
                    QC::u64 when = 0;
                    if (parseU64(value, when))
                        e.scheduledAtMs = when;
                    continue;
                }
                if (parsePrefixedValue(dep, "direct=", value, sizeof(value)))
                {
                    e.directExecAllowed = (QC::String::strcmp(value, "allow") == 0);
                    continue;
                }
                if (parsePrefixedValue(dep, "ns=", value, sizeof(value)))
                {
                    QC::String::strncpy(e.namespaceId, value, sizeof(e.namespaceId) - 1);
                    continue;
                }
                if (parsePrefixedValue(dep, "ver=", value, sizeof(value)))
                {
                    (void)parseVersion(value, e.versionMajor, e.versionMinor);
                    continue;
                }
                if (parsePrefixedValue(dep, "internal=", value, sizeof(value)))
                {
                    e.internalOnly = (QC::String::strcmp(value, "1") == 0 || QC::String::strcmp(value, "true") == 0);
                    continue;
                }

                QC::String::strncpy(e.deps[e.depCount], dep, sizeof(e.deps[e.depCount]) - 1);
                e.deps[e.depCount][sizeof(e.deps[e.depCount]) - 1] = '\0';
                ++e.depCount;
            }
            g_catalog.entries.push_back(e);
        };

        const char *p = content.data();
        while (p && *p)
        {
            char c = *p++;
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                line[li] = '\0';
                parseLine(line);
                li = 0;
                line[0] = '\0';
                continue;
            }
            if (li + 1 < sizeof(line))
                line[li++] = c;
        }
        if (li)
        {
            line[li] = '\0';
            parseLine(line);
        }

        g_catalog.loaded = true;
        return QC::Status::Success;
    }

    QC::Status Loader::fetchWithDependencies(const char *moduleId, FetchReport *outReport)
    {
        if (!moduleId || *moduleId == '\0')
            return QC::Status::InvalidParam;

        const QC::Status st = loadCatalog();
        if (st != QC::Status::Success)
            return st;

        const int root = findModule(moduleId);
        if (root < 0)
            return QC::Status::NotFound;

        bool visiting[64] = {false};
        bool visited[64] = {false};
        FetchReport rep{};
        const bool ok = fetchRec(root, visiting, visited, &rep);
        if (!ok)
            return QC::Status::Error;

        if (outReport)
            *outReport = rep;
        return QC::Status::Success;
    }

    QC::Status Loader::load(const char *moduleId, FetchReport *outReport)
    {
        if (!moduleId || *moduleId == '\0')
            return QC::Status::InvalidParam;

        const QC::Status st = fetchWithDependencies(moduleId, outReport);
        if (st != QC::Status::Success)
            return st;

        if (enforceSafeCutoverBoundary() != QC::Status::Success)
            return QC::Status::Timeout;

        const int root = findModule(moduleId);
        if (root < 0)
            return QC::Status::NotFound;

        ModuleEntry &rootEntry = g_catalog.entries[static_cast<QC::usize>(root)];
        if (rootEntry.internalOnly)
        {
            QC::String::strncpy(g_lastInspection.detail, "internal-only namespace is not user-loadable", sizeof(g_lastInspection.detail) - 1);
            return QC::Status::Error;
        }
        if (!rootEntry.directExecAllowed)
        {
            QC::String::strncpy(g_lastInspection.detail, "direct execution denied for downloaded/update module", sizeof(g_lastInspection.detail) - 1);
            return QC::Status::Error;
        }
        if (rootEntry.scheduledAtMs != 0)
        {
            const QC::u64 now = QK::Time::milliseconds();
            if (now < rootEntry.scheduledAtMs)
            {
                QC::String::strncpy(g_lastInspection.detail, "module update is scheduled for later application", sizeof(g_lastInspection.detail) - 1);
                return QC::Status::Busy;
            }
        }

        bool seen[64] = {false};
        if (!allSandboxedRec(root, seen))
        {
            QC::String::strncpy(g_lastInspection.detail, "sandbox-only first run required", sizeof(g_lastInspection.detail) - 1);
            return QC::Status::Busy;
        }

        return QC::Status::Success;
    }

    QC::Status Loader::loadSandboxed(const char *moduleId, FetchReport *outReport)
    {
        if (!moduleId || *moduleId == '\0')
            return QC::Status::InvalidParam;

        const QC::Status st = fetchWithDependencies(moduleId, outReport);
        if (st != QC::Status::Success)
            return st;

        if (enforceSafeCutoverBoundary() != QC::Status::Success)
            return QC::Status::Timeout;

        const int root = findModule(moduleId);
        if (root < 0)
            return QC::Status::NotFound;

        ModuleEntry &rootEntry = g_catalog.entries[static_cast<QC::usize>(root)];
        if (rootEntry.internalOnly)
        {
            QC::String::strncpy(g_lastInspection.detail, "internal-only namespace is not user-loadable", sizeof(g_lastInspection.detail) - 1);
            return QC::Status::Error;
        }
        if (rootEntry.scheduledAtMs != 0)
        {
            const QC::u64 now = QK::Time::milliseconds();
            if (now < rootEntry.scheduledAtMs)
            {
                QC::String::strncpy(g_lastInspection.detail, "module update is scheduled for later application", sizeof(g_lastInspection.detail) - 1);
                return QC::Status::Busy;
            }
        }

        bool seen[64] = {false};
        if (!markSandboxedRec(root, seen))
            return QC::Status::Error;

        QC::String::strncpy(g_lastInspection.detail, "sandbox first run accepted", sizeof(g_lastInspection.detail) - 1);
        return QC::Status::Success;
    }

    QC::Status Loader::unload(const char *moduleId)
    {
        if (!moduleId || *moduleId == '\0')
            return QC::Status::InvalidParam;

        const QC::Status st = loadCatalog();
        if (st != QC::Status::Success)
            return st;

        const int index = findModule(moduleId);
        if (index < 0)
            return QC::Status::NotFound;

        ModuleEntry &e = g_catalog.entries[static_cast<QC::usize>(index)];
        if (!e.fetched)
            return QC::Status::NotFound;

        if (hasLoadedDependents(index))
            return QC::Status::Busy;

        e.fetched = false;
        e.bytes = 0;
        return QC::Status::Success;
    }

    QC::usize Loader::listLoaded(LoadedModule *outModules, QC::usize cap)
    {
        if (loadCatalog() != QC::Status::Success)
            return 0;

        QC::usize count = 0;
        for (QC::usize i = 0; i < g_catalog.entries.size(); ++i)
        {
            const ModuleEntry &e = g_catalog.entries[i];
            if (!e.fetched)
                continue;

            if (outModules && count < cap)
            {
                QC::String::memset(&outModules[count], 0, sizeof(LoadedModule));
                QC::String::strncpy(outModules[count].id, e.id, sizeof(outModules[count].id) - 1);
                QC::String::strncpy(outModules[count].path, e.path, sizeof(outModules[count].path) - 1);
                outModules[count].bytes = e.bytes;
                outModules[count].depCount = e.depCount;
            }
            ++count;
        }

        return count;
    }

    QC::usize Loader::buildDependencyGraph(const char *moduleId, DependencyEdge *outEdges, QC::usize edgeCap)
    {
        if (!moduleId || *moduleId == '\0' || !outEdges || edgeCap == 0)
            return 0;

        if (loadCatalog() != QC::Status::Success)
            return 0;

        const int root = findModule(moduleId);
        if (root < 0)
            return 0;

        bool seen[64] = {false};
        QC::usize count = 0;
        graphRec(root, seen, outEdges, edgeCap, count);
        return count;
    }

    QC::Status Loader::lastInspectionState(InspectionState &out) const
    {
        out = g_lastInspection;
        return QC::Status::Success;
    }
}
