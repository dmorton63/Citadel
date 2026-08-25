// QKConsole - Minimal kernel console implementation

#include "QKConsole.h"
#include "QFSDirectory.h"
#include "QFSFile.h"
#include "QFSVolumeManager.h"
#include "QFSVFS.h"
#include "QCString.h"

#include "QCCommandRegistry.h"
#include "QKCommandCenter.h"

#include "QKSystemVolumeCommands.h"
#include "QKDrvManager.h"
#include "Debug/Serial/QKDebugSerial.h"

#include "Debug/Framebuffer/QKDebugFramebufferText.h"

#include "QKShutdownController.h"

#include "QKBootConfigTier.h"
#include "QKBootLog.h"
#include "Boot/Config/QKBootStagedConfig.h"
#include "Boot/Config/QKBootStartupConfig.h"
#include "Boot/Desktop/QKBootDesktopSession.h"

#include "QArchCPU.h"

namespace QK
{
    namespace Console
    {
        namespace
        {
            void print(const char *msg);
            bool appendString(char *dest, QC::usize destSize, const char *src);
            bool resolvePath(const char *input, char *out, QC::usize outSize);

            static inline char lowerAscii(char c)
            {
                if (c >= 'A' && c <= 'Z')
                    return static_cast<char>(c + 32);
                return c;
            }

            static bool resolveOutputPath(const char *arg, char *path, QC::usize pathCap, const char *defaultPath)
            {
                if (!path || pathCap == 0)
                    return false;

                QC::String::memset(path, 0, pathCap);

                const char *preferredRoot = nullptr;
                if (QFS::VolumeManager::instance().isMounted("QFS_SYSTEM"))
                    preferredRoot = "/system";
                else if (QFS::VolumeManager::instance().isMounted("QFS_SHARED"))
                    preferredRoot = "/shared";

                if (!arg || !*arg)
                {
                    if (preferredRoot)
                    {
                        QC::String::strncpy(path, preferredRoot, pathCap - 1);
                        appendString(path, pathCap, "/");
                        appendString(path, pathCap, defaultPath);
                    }
                    else
                    {
                        QC::String::strncpy(path, "/shared/", pathCap - 1);
                        appendString(path, pathCap, defaultPath);
                    }
                    return true;
                }

                bool hasSlash = false;
                for (const char *p = arg; *p; ++p)
                {
                    if (*p == '/')
                    {
                        hasSlash = true;
                        break;
                    }
                }

                if (!hasSlash)
                {
                    if (preferredRoot)
                    {
                        QC::String::strncpy(path, preferredRoot, pathCap - 1);
                        appendString(path, pathCap, "/");
                    }
                    else
                    {
                        QC::String::strncpy(path, "/shared/", pathCap - 1);
                    }
                    appendString(path, pathCap, arg);
                    return true;
                }

                return resolvePath(arg, path, pathCap);
            }

            static void printByteCount(QC::isize wrote)
            {
                char num[32];
                QC::String::memset(num, 0, sizeof(num));
                QC::u64 v = (wrote < 0) ? 0 : static_cast<QC::u64>(wrote);
                int numIdx = 0;
                if (v == 0)
                {
                    num[numIdx++] = '0';
                }
                else
                {
                    char tmp[32];
                    int tmpIdx = 0;
                    while (v > 0 && tmpIdx < 31)
                    {
                        tmp[tmpIdx++] = static_cast<char>('0' + (v % 10));
                        v /= 10;
                    }
                    for (int i = tmpIdx - 1; i >= 0; --i)
                        num[numIdx++] = tmp[i];
                }
                num[numIdx] = '\0';
                print(num);
            }

            static bool streqIgnoreCase(const char *a, const char *b)
            {
                if (!a || !b)
                    return a == b;
                while (*a && *b)
                {
                    if (lowerAscii(*a) != lowerAscii(*b))
                        return false;
                    ++a;
                    ++b;
                }
                return *a == '\0' && *b == '\0';
            }

            static bool pathHasPrefix(const char *path, const char *prefix)
            {
                if (!path || !prefix)
                    return false;
                const QC::usize n = QC::String::strlen(prefix);
                if (QC::String::memcmp(path, prefix, n) != 0)
                    return false;
                return path[n] == '\0' || path[n] == '/';
            }

            static const char *persistenceClassForPath(const char *path)
            {
                if (!path || path[0] != '/')
                    return "unknown";

                if (pathHasPrefix(path, "/system"))
                    return "persistent";
                if (pathHasPrefix(path, "/shared"))
                    return "ephemeral";

                QFS::VolumeInfo volumes[32] = {};
                const QC::usize count = QFS::VolumeManager::instance().copyVolumeInfo(volumes, sizeof(volumes) / sizeof(volumes[0]));
                const char *bestClass = nullptr;
                QC::usize bestLen = 0;
                for (QC::usize i = 0; i < count; ++i)
                {
                    if (!volumes[i].mounted)
                        continue;
                    if (!pathHasPrefix(path, volumes[i].mountPath))
                        continue;
                    const QC::usize n = QC::String::strlen(volumes[i].mountPath);
                    if (!bestClass || n > bestLen)
                    {
                        bestClass = volumes[i].persistenceClass[0] ? volumes[i].persistenceClass : "unknown";
                        bestLen = n;
                    }
                }

                return bestClass ? bestClass : "unknown";
            }
            constexpr QC::usize kBufferSize = 256;
            char g_buffer[kBufferSize];
            QC::usize g_length = 0;
            PrintFn g_printer = nullptr;

            // Optional blocking line-capture mode (used for boot-time prompts).
            bool g_captureActive = false;
            bool g_captureEcho = true;
            volatile bool g_captureReady = false;
            char g_captureLine[kBufferSize];
            constexpr QC::usize kMaxCommands = 32;
            Command g_commandTable[kMaxCommands];
            QC::usize g_commandCount = 0;

            constexpr QC::usize kCwdSize = 128;
            char g_cwd[kCwdSize] = "/";

            // Minimal role model (MVP): affects registry command access + help filtering.
            // Default to Admin until real auth/session model exists.
            QC::Cmd::AccessLevel g_role = QC::Cmd::AccessLevel::Admin;

            constexpr QC::usize kMaxArgs = 16;

            constexpr QC::usize kTranscriptSize = 64 * 1024;
            char g_transcript[kTranscriptSize];
            QC::usize g_transcriptLen = 0;
            bool g_transcriptTruncated = false;

            QC::usize g_savetermLastSavedLen = 0;
            bool g_savetermHasBaseline = false;
            char g_savetermLastPath[256];

            bool g_inputEnabled = true;
            bool g_safeFallbackEnabled = false;
            Console::Owner g_owner = Console::Owner::Boot;

            bool g_warnedAdminEnable = false;
            bool g_warnedSystemEnable = false;

            void appendTranscript(const char *msg)
            {
                if (!msg || *msg == '\0')
                    return;
                if (g_transcriptLen + 1 >= kTranscriptSize)
                {
                    g_transcriptTruncated = true;
                    return;
                }

                const QC::usize avail = kTranscriptSize - g_transcriptLen - 1;
                QC::usize n = QC::String::strlen(msg);
                if (n > avail)
                {
                    n = avail;
                    g_transcriptTruncated = true;
                }
                if (n > 0)
                {
                    QC::String::memcpy(g_transcript + g_transcriptLen, msg, n);
                    g_transcriptLen += n;
                    g_transcript[g_transcriptLen] = '\0';
                }
            }

            void print(const char *msg)
            {
                if (g_printer)
                {
                    g_printer(msg);
                }
                appendTranscript(msg);
            }

            bool isSpace(char c)
            {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            }

            void printPrompt()
            {
                print("\r\nCITADEL> ");
            }

            void clearBuffer()
            {
                QC::String::memset(g_buffer, 0, sizeof(g_buffer));
                g_length = 0;
            }

            void appendChar(char c)
            {
                if (g_length + 1 >= kBufferSize)
                    return;
                g_buffer[g_length++] = c;
                g_buffer[g_length] = '\0';

                if (!g_captureActive || g_captureEcho)
                {
                    char out[2] = {c, '\0'};
                    print(out);
                }
            }

            void backspace()
            {
                if (g_length == 0)
                    return;
                --g_length;
                g_buffer[g_length] = '\0';

                if (!g_captureActive || g_captureEcho)
                    print("\b \b");
            }

            void listDirectory(const char *path)
            {
                QFS::Directory *dir = QFS::VFS::instance().openDir(path);
                if (!dir)
                {
                    print("ls: cannot open directory\r\n");
                    return;
                }

                print("\r\n");
                QFS::DirEntry entry;
                while (dir->read(&entry))
                {
                    char line[320];
                    QC::String::memset(line, 0, sizeof(line));
                    QC::usize idx = 0;

                    const char *type = "-";
                    if (entry.type == QFS::FileType::Directory)
                        type = "d";
                    else if (entry.type == QFS::FileType::SymLink)
                        type = "l";

                    line[idx++] = type[0];
                    line[idx++] = ' ';

                    char sizeBuf[32];
                    QC::String::memset(sizeBuf, 0, sizeof(sizeBuf));
                    QC::u64 size = entry.size;
                    int sizeIdx = 0;
                    if (size == 0)
                    {
                        sizeBuf[sizeIdx++] = '0';
                    }
                    else
                    {
                        char temp[32];
                        int tempIdx = 0;
                        while (size > 0 && tempIdx < 31)
                        {
                            temp[tempIdx++] = '0' + (size % 10);
                            size /= 10;
                        }
                        for (int i = tempIdx - 1; i >= 0; --i)
                        {
                            sizeBuf[sizeIdx++] = temp[i];
                        }
                    }
                    sizeBuf[sizeIdx] = '\0';

                    for (int i = 0; sizeBuf[i] && idx < static_cast<int>(sizeof(line) - 1); ++i)
                    {
                        line[idx++] = sizeBuf[i];
                    }
                    line[idx++] = ' ';

                    for (int i = 0; entry.name[i] && idx < static_cast<int>(sizeof(line) - 2); ++i)
                    {
                        line[idx++] = entry.name[i];
                    }
                    line[idx++] = '\r';
                    line[idx++] = '\n';
                    line[idx] = '\0';
                    print(line);
                }

                QFS::VFS::instance().closeDir(dir);
            }

            void resetCommandTable()
            {
                g_commandCount = 0;
                QC::String::memset(g_commandTable, 0, sizeof(g_commandTable));
            }

            const Command *findCommand(const char *name)
            {
                if (!name)
                    return nullptr;
                for (QC::usize i = 0; i < g_commandCount; ++i)
                {
                    if (streqIgnoreCase(g_commandTable[i].name, name))
                        return &g_commandTable[i];
                }
                return nullptr;
            }

            bool addCommandInternal(const Command &cmd)
            {
                if (!cmd.name || !cmd.handler || QC::String::strlen(cmd.name) == 0)
                    return false;
                if (findCommand(cmd.name))
                    return false;
                if (g_commandCount >= kMaxCommands)
                    return false;
                g_commandTable[g_commandCount++] = cmd;
                return true;
            }

            const char *skipSpaces(const char *str)
            {
                while (str && isSpace(*str))
                    ++str;
                return str;
            }

            bool segmentEquals(const char *start, QC::usize len, const char *literal)
            {
                if (!start || !literal)
                    return false;
                QC::usize litLen = QC::String::strlen(literal);
                if (len != litLen)
                    return false;
                for (QC::usize i = 0; i < len; ++i)
                {
                    if (start[i] != literal[i])
                        return false;
                }
                return true;
            }

            bool appendString(char *dest, QC::usize destSize, const char *src)
            {
                if (!dest || destSize == 0)
                    return false;
                if (!src || *src == '\0')
                    return true;

                QC::usize destLen = QC::String::strlen(dest);
                QC::usize srcLen = QC::String::strlen(src);
                if (destLen >= destSize)
                    return false;

                QC::usize remaining = destSize - destLen - 1;
                if (srcLen > remaining)
                {
                    QC::String::memcpy(dest + destLen, src, remaining);
                    dest[destLen + remaining] = '\0';
                    return false;
                }

                QC::String::memcpy(dest + destLen, src, srcLen);
                dest[destLen + srcLen] = '\0';
                return true;
            }

            bool normalizeAbsolutePath(const char *input, char *out, QC::usize outSize)
            {
                if (!input || !out || outSize == 0)
                    return false;
                if (input[0] != '/')
                    return false;

                const char *segments[32];
                QC::usize segLens[32];
                QC::usize segCount = 0;

                const char *p = input;
                while (*p)
                {
                    while (*p == '/')
                        ++p;
                    if (*p == '\0')
                        break;

                    const char *segStart = p;
                    while (*p && *p != '/')
                        ++p;
                    QC::usize segLen = static_cast<QC::usize>(p - segStart);

                    if (segLen == 0 || segmentEquals(segStart, segLen, "."))
                    {
                        continue;
                    }
                    if (segmentEquals(segStart, segLen, ".."))
                    {
                        if (segCount > 0)
                            --segCount;
                        continue;
                    }
                    if (segCount < 32)
                    {
                        segments[segCount] = segStart;
                        segLens[segCount] = segLen;
                        ++segCount;
                    }
                }

                QC::usize idx = 0;
                if (outSize < 2)
                    return false;
                out[idx++] = '/';

                for (QC::usize i = 0; i < segCount; ++i)
                {
                    if (i != 0)
                    {
                        if (idx + 1 >= outSize)
                            return false;
                        out[idx++] = '/';
                    }

                    if (idx + segLens[i] >= outSize)
                        return false;
                    for (QC::usize j = 0; j < segLens[i]; ++j)
                    {
                        out[idx++] = segments[i][j];
                    }
                }

                out[idx] = '\0';
                return true;
            }

            bool resolvePath(const char *input, char *out, QC::usize outSize)
            {
                if (!out || outSize == 0)
                    return false;

                const char *arg = input;
                if (!arg || *arg == '\0')
                    arg = ".";

                char tmp[256];
                QC::String::memset(tmp, 0, sizeof(tmp));

                if (arg[0] == '/')
                {
                    QC::String::strncpy(tmp, arg, sizeof(tmp) - 1);
                }
                else
                {
                    if (QC::String::strcmp(g_cwd, "/") == 0)
                    {
                        QC::String::strncpy(tmp, "/", sizeof(tmp) - 1);
                        if (!appendString(tmp, sizeof(tmp), arg))
                            return false;
                    }
                    else
                    {
                        QC::String::strncpy(tmp, g_cwd, sizeof(tmp) - 1);
                        if (!appendString(tmp, sizeof(tmp), "/"))
                            return false;
                        if (!appendString(tmp, sizeof(tmp), arg))
                            return false;
                    }
                }

                return normalizeAbsolutePath(tmp, out, outSize);
            }

            bool tokenizeInPlace(char *line, const char **argv, int &argc)
            {
                argc = 0;
                if (!line || !argv)
                    return false;

                char *p = line;
                while (*p)
                {
                    while (*p && isSpace(*p))
                        ++p;
                    if (*p == '\0')
                        break;

                    if (argc >= static_cast<int>(kMaxArgs))
                        break;

                    if (*p == '"')
                    {
                        ++p;
                        argv[argc++] = p;
                        while (*p && *p != '"')
                            ++p;
                        if (*p == '"')
                        {
                            *p = '\0';
                            ++p;
                        }
                        continue;
                    }

                    argv[argc++] = p;
                    while (*p && !isSpace(*p))
                        ++p;
                    if (*p)
                    {
                        *p = '\0';
                        ++p;
                    }
                }

                return argc > 0;
            }

            void handleLs(int argc, const char *const *argv)
            {
                const char *arg = (argc >= 2) ? argv[1] : ".";
                char path[256];
                QC::String::memset(path, 0, sizeof(path));
                if (!resolvePath(arg, path, sizeof(path)))
                {
                    print("ls: invalid path\r\n");
                    return;
                }
                listDirectory(path);
            }

            void handleClear(int, const char *const *)
            {
                print("\033[2J\033[H");
            }

            void handleHelp(int argc, const char *const *argv)
            {
                const QC::Cmd::AccessLevel caller = g_role;
                const char *query = (argc >= 2) ? argv[1] : nullptr;
                if (query && *query)
                {
                    const Command *cmd = findCommand(query);
                    if (!cmd)
                    {
                        QK::CmdCenter::registerMvpCommands();
                        auto &reg = QC::Cmd::Registry::instance();
                        bool found = false;
                        for (QC::usize i = 0; i < reg.commandCount(); ++i)
                        {
                            const char *name = reg.commandNameAt(i);
                            if (!name)
                                continue;
                            if (!streqIgnoreCase(name, query))
                                continue;
                            found = true;
                            if (static_cast<QC::u8>(caller) < static_cast<QC::u8>(reg.commandAccessAt(i)))
                            {
                                print("\r\nPermission denied\r\n");
                                return;
                            }

                            const char *desc = reg.commandDescriptionAt(i);
                            print("\r\n");
                            print(query);
                            if (desc && *desc)
                            {
                                print(" - ");
                                print(desc);
                            }
                            print("\r\n");
                            return;
                        }
                        if (!found)
                            print("\r\nCommand not found\r\n");
                        return;
                    }
                    print("\r\n");
                    print(cmd->name);
                    if (cmd->description && *cmd->description)
                    {
                        print(" - ");
                        print(cmd->description);
                    }
                    print("\r\n");
                    return;
                }

                print("\r\nCommands:\r\n");
                for (QC::usize i = 0; i < g_commandCount; ++i)
                {
                    print("  ");
                    print(g_commandTable[i].name);
                    if (g_commandTable[i].description && *g_commandTable[i].description)
                    {
                        print(" - ");
                        print(g_commandTable[i].description);
                    }
                    print("\r\n");
                }

                // Add shared registry commands (if any) that aren't built-ins.
                QK::CmdCenter::registerMvpCommands();
                auto &reg = QC::Cmd::Registry::instance();
                for (QC::usize i = 0; i < reg.commandCount(); ++i)
                {
                    const char *name = reg.commandNameAt(i);
                    if (!name)
                        continue;
                    if (findCommand(name))
                        continue;
                    if (static_cast<QC::u8>(caller) < static_cast<QC::u8>(reg.commandAccessAt(i)))
                        continue;

                    print("  ");
                    print(name);

                    const char *desc = reg.commandDescriptionAt(i);
                    if (desc && *desc)
                    {
                        print(" - ");
                        print(desc);
                    }
                    print("\r\n");
                }
            }

            void handlePwd(int, const char *const *)
            {
                print("\r\n");
                print(g_cwd);
                print("\r\n");
            }

            void handleCd(int argc, const char *const *argv)
            {
                const char *arg = (argc >= 2) ? argv[1] : "/";
                char path[256];
                QC::String::memset(path, 0, sizeof(path));
                if (!resolvePath(arg, path, sizeof(path)))
                {
                    print("cd: invalid path\r\n");
                    return;
                }

                QFS::Directory *dir = QFS::VFS::instance().openDir(path);
                if (!dir)
                {
                    print("cd: no such directory\r\n");
                    return;
                }
                QFS::VFS::instance().closeDir(dir);

                QC::String::strncpy(g_cwd, path, sizeof(g_cwd) - 1);
                g_cwd[sizeof(g_cwd) - 1] = '\0';
            }

            void handleCat(int argc, const char *const *argv)
            {
                if (argc < 2 || !argv[1] || !*argv[1])
                {
                    print("cat: missing file operand\r\n");
                    return;
                }

                char path[256];
                QC::String::memset(path, 0, sizeof(path));
                if (!resolvePath(argv[1], path, sizeof(path)))
                {
                    print("cat: invalid path\r\n");
                    return;
                }

                QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
                if (!file)
                {
                    print("cat: cannot open file\r\n");
                    return;
                }

                print("\r\n");

                char inBuf[256];
                char outBuf[512];
                while (true)
                {
                    QC::isize n = file->read(inBuf, sizeof(inBuf));
                    if (n <= 0)
                        break;

                    QC::usize outIdx = 0;
                    for (QC::isize i = 0; i < n; ++i)
                    {
                        char c = inBuf[i];
                        if (c == '\n')
                        {
                            if (outIdx + 2 >= sizeof(outBuf))
                            {
                                outBuf[outIdx] = '\0';
                                print(outBuf);
                                outIdx = 0;
                            }
                            outBuf[outIdx++] = '\r';
                            outBuf[outIdx++] = '\n';
                            continue;
                        }

                        if (outIdx + 1 >= sizeof(outBuf))
                        {
                            outBuf[outIdx] = '\0';
                            print(outBuf);
                            outIdx = 0;
                        }
                        outBuf[outIdx++] = c;
                    }

                    outBuf[outIdx] = '\0';
                    if (outIdx > 0)
                        print(outBuf);
                }

                QFS::VFS::instance().close(file);
                print("\r\n");
            }

            void handleTouch(int argc, const char *const *argv)
            {
                if (static_cast<QC::u8>(g_role) < static_cast<QC::u8>(QC::Cmd::AccessLevel::Admin))
                {
                    print("touch: permission denied\r\n");
                    return;
                }

                if (argc < 2 || !argv[1] || !*argv[1])
                {
                    print("touch: missing file operand\r\n");
                    return;
                }

                char path[256];
                QC::String::memset(path, 0, sizeof(path));
                if (!resolvePath(argv[1], path, sizeof(path)))
                {
                    print("touch: invalid path\r\n");
                    return;
                }

                const QC::usize len = QC::String::strlen(path);
                if (len == 0 || path[len - 1] == '/')
                {
                    print("touch: invalid path\r\n");
                    return;
                }

                // Create if missing; if it already exists, do not truncate.
                QFS::File *file = QFS::VFS::instance().open(path, QFS::OpenMode::Write | QFS::OpenMode::Create);
                if (!file)
                {
                    print("touch: cannot create file\r\n");
                    return;
                }

                QFS::VFS::instance().close(file);
            }

            void handleSaveTerm(int argc, const char *const *argv)
            {
                bool appendMode = false;
                const char *arg = nullptr;

                if (argc >= 2 && streqIgnoreCase(argv[1], "append"))
                {
                    appendMode = true;
                    arg = (argc >= 3) ? argv[2] : nullptr;
                }
                else
                {
                    arg = (argc >= 2) ? argv[1] : nullptr;
                }

                char path[256];

                if ((!arg || !*arg) && appendMode && g_savetermHasBaseline && g_savetermLastPath[0])
                {
                    QC::String::strncpy(path, g_savetermLastPath, sizeof(path) - 1);
                }
                else
                {
                    if (!resolveOutputPath(arg, path, sizeof(path), "citadel.txt"))
                    {
                        print("saveterm: invalid path\r\n");
                        return;
                    }
                }

                QFS::OpenMode mode = QFS::OpenMode::Write | QFS::OpenMode::Create;
                if (!appendMode)
                    mode = mode | QFS::OpenMode::Truncate;

                QFS::File *file = QFS::VFS::instance().open(path, mode);
                if (!file)
                {
                    print("saveterm: cannot open output file: ");
                    print(path);
                    print(" (is the target path mounted + writable?)\r\n");
                    print("saveterm: persistence=");
                    print(persistenceClassForPath(path));
                    print("\r\n");
                    return;
                }

                if (appendMode)
                {
                    (void)file->seek(0, QFS::SeekOrigin::End);
                }

                if (!appendMode && g_transcriptTruncated)
                {
                    const char *hdr = "[transcript truncated]\r\n";
                    (void)file->write(hdr, QC::String::strlen(hdr));
                }

                QC::usize start = 0;
                if (appendMode && g_savetermHasBaseline && QC::String::strcmp(path, g_savetermLastPath) == 0)
                {
                    start = g_savetermLastSavedLen;
                    if (start > g_transcriptLen)
                        start = 0;
                }

                QC::isize wrote = 0;
                if (g_transcriptLen > start)
                {
                    wrote = file->write(g_transcript + start, g_transcriptLen - start);
                }

                QFS::VFS::instance().close(file);

                print("\r\nsaveterm: wrote ");
                printByteCount(wrote);
                print(" bytes to ");
                print(path);
                print(" (persistence=");
                print(persistenceClassForPath(path));
                print(")\r\n");

                // Skip the status output we just printed.
                g_savetermLastSavedLen = g_transcriptLen;
                g_savetermHasBaseline = true;
                QC::String::memset(g_savetermLastPath, 0, sizeof(g_savetermLastPath));
                QC::String::strncpy(g_savetermLastPath, path, sizeof(g_savetermLastPath) - 1);
            }

            void handleSaveSerial(int argc, const char *const *argv)
            {
                const char *arg = (argc >= 2) ? argv[1] : nullptr;

                char path[256];
                if (!resolveOutputPath(arg, path, sizeof(path), "citadel-serial.txt"))
                {
                    print("saveserial: invalid path\r\n");
                    return;
                }

                QFS::File *file = QFS::VFS::instance().open(path,
                                                            QFS::OpenMode::Write |
                                                                QFS::OpenMode::Create |
                                                                QFS::OpenMode::Truncate);
                if (!file)
                {
                    print("saveserial: cannot open output file: ");
                    print(path);
                    print(" (is the target path mounted + writable?)\r\n");
                    print("saveserial: persistence=");
                    print(persistenceClassForPath(path));
                    print("\r\n");
                    return;
                }

                QC::isize wrote = 0;
                if (QK::Debug::Serial::CaptureTruncated())
                {
                    const char *hdr = "[serial capture truncated]\r\n";
                    wrote += file->write(hdr, QC::String::strlen(hdr));
                }

                const char *capture = QK::Debug::Serial::CaptureData();
                const QC::usize captureLen = QK::Debug::Serial::CaptureSize();
                if (capture && captureLen > 0)
                {
                    wrote += file->write(capture, captureLen);
                }

                QFS::VFS::instance().close(file);

                print("\r\nsaveserial: wrote ");
                printByteCount(wrote);
                print(" bytes to ");
                print(path);
                print(" (persistence=");
                print(persistenceClassForPath(path));
                print(")\r\n");
            }

            void handleTier(int, const char *const *)
            {
                print("\r\nActive tier: '");
                print(QK::Boot::Config::GetActiveConfigTierName());
                print("' root='");
                print(QK::Boot::Config::GetActiveConfigTierRoot());
                print("'\r\n");

                const auto *stage = QK::Boot::Config::GetCommittedEarlyConfig();
                if (!stage)
                {
                    print("Committed early config: (none)\r\n");
                    return;
                }

                print("Committed early config: root='");
                print(stage->root[0] ? stage->root : "(none)");
                print("' modules=");

                char num[32];
                QC::String::memset(num, 0, sizeof(num));
                QC::u64 v = static_cast<QC::u64>(stage->moduleCount);
                int numIdx = 0;
                if (v == 0)
                {
                    num[numIdx++] = '0';
                }
                else
                {
                    char tmp[32];
                    int tmpIdx = 0;
                    while (v > 0 && tmpIdx < 31)
                    {
                        tmp[tmpIdx++] = static_cast<char>('0' + (v % 10));
                        v /= 10;
                    }
                    for (int i = tmpIdx - 1; i >= 0; --i)
                        num[numIdx++] = tmp[i];
                }
                num[numIdx] = '\0';
                print(num);
                print("\r\n");

                for (QC::u32 i = 0; i < stage->moduleCount && i < 16; ++i)
                {
                    const auto &m = stage->modules[i];
                    if (m.id[0] == 0)
                        continue;

                    print("- id='");
                    print(m.id);
                    print("' type='");
                    print(m.type);
                    print("' required=");
                    print(m.required ? "true" : "false");
                    print(" json=");
                    print(m.hasJson ? "true" : "false");
                    print(" path='");
                    print(m.resolvedPath);
                    print("'\r\n");
                }
            }

            void registerBuiltIns()
            {
                addCommandInternal({"ls", handleLs, "List directory contents"});
                addCommandInternal({"clear", handleClear, "Clear the console"});
                addCommandInternal({"help", handleHelp, "Show available commands"});
                addCommandInternal({"shutdown",
                                    [](int, const char *const *) {
                                        print("\r\nShutdown requested.\r\n");
                                        QK::Shutdown::Controller::instance().requestShutdown(QK::Shutdown::Reason::ShellCommand);
                                    },
                                    "Request shutdown"});
                addCommandInternal({"startx",
                                    [](int, const char *const *) {
                                        const auto mode = QK::Boot::Config::GetStartupMode();
                                        if (mode == QK::Boot::Config::StartupMode::Desktop)
                                        {
                                            print("\r\nstartx: already in DESKTOP startup mode\r\n");
                                            return;
                                        }
                                        if (mode != QK::Boot::Config::StartupMode::Terminal)
                                        {
                                            print("\r\nstartx: only supported in TERMINAL startup mode\r\n");
                                            return;
                                        }
                                        if (!QK::Boot::Desktop::IsPrepared())
                                        {
                                            print("\r\nstartx: no framebuffer available\r\n");
                                            return;
                                        }

                                        print("\r\nstartx: launching desktop...\r\n");

                                        // Hand off keyboard to the window/event system.
                                        QK::Console::setInputEnabled(false);
                                        QK::Debug::FramebufferText::SetEnabled(false);

                                        QK::Boot::Desktop::InitializeWindowSystem();
                                        QK::Boot::Desktop::InitializeDesktopAndRunLoop();
                                    },
                                    "Start desktop session (TERMINAL startup mode only)"});
                addCommandInternal({"pwd", handlePwd, "Print current working directory"});
                addCommandInternal({"cd", handleCd, "Change current directory"});
                addCommandInternal({"cat", handleCat, "Print file contents"});
                addCommandInternal({"touch", handleTouch, "Create empty file"});
                addCommandInternal({"saveterm", handleSaveTerm, "Save console transcript to a writable path (prefers /system)"});
                addCommandInternal({"saveserial", handleSaveSerial, "Save mirrored serial boot log to a writable path (prefers /system)"});
                addCommandInternal({"tier", handleTier, "Show active config tier + staged early modules"});

                addCommandInternal({"whoami",
                                    [](int, const char *const *) {
                                        print("\r\nrole=");
                                        switch (g_role)
                                        {
                                        case QC::Cmd::AccessLevel::User:
                                            print("user");
                                            break;
                                        case QC::Cmd::AccessLevel::Admin:
                                            print("admin");
                                            break;
                                        case QC::Cmd::AccessLevel::SysAdmin:
                                            print("su");
                                            break;
                                        case QC::Cmd::AccessLevel::System:
                                            print("system");
                                            break;
                                        default:
                                            print("?");
                                            break;
                                        }
                                        print("\r\n");
                                    },
                                    "Show current role"});

                addCommandInternal({"user",
                                    [](int, const char *const *) {
                                        g_role = QC::Cmd::AccessLevel::User;
                                        print("\r\nrole=user\r\n");
                                    },
                                    "Set role to user"});
                addCommandInternal({"admin",
                                    [](int argc, const char *const *argv) {
                                        const auto mode = QK::Boot::Config::GetStartupMode();
                                        if (mode != QK::Boot::Config::StartupMode::Terminal && !g_safeFallbackEnabled)
                                        {
                                            print("\r\nadmin: only available in TERMINAL startup mode\r\n");
                                            return;
                                        }

                                        if (argc < 2 || !argv[1] || !streqIgnoreCase(argv[1], "enable"))
                                        {
                                            g_warnedAdminEnable = true;
                                            print("\r\nWARNING: admin mode can modify system state.\r\n");
                                            print("Type: admin enable\r\n");
                                            return;
                                        }

                                        if (!g_warnedAdminEnable)
                                        {
                                            g_warnedAdminEnable = true;
                                            print("\r\nWARNING: admin mode can modify system state.\r\n");
                                            print("Type: admin enable\r\n");
                                            return;
                                        }

                                        g_role = QC::Cmd::AccessLevel::Admin;
                                        print("\r\nrole=admin\r\n");
                                    },
                                    "Enable admin role (TERMINAL only; run twice)"});
                addCommandInternal({"su",
                                    [](int argc, const char *const *argv) {
                                        const auto mode = QK::Boot::Config::GetStartupMode();
                                        if (mode != QK::Boot::Config::StartupMode::Recovery)
                                        {
                                            print("\r\nsu: only available in RECOVERY startup mode\r\n");
                                            return;
                                        }

                                        if (argc < 2 || !argv[1] || !streqIgnoreCase(argv[1], "enable"))
                                        {
                                            g_warnedSystemEnable = true;
                                            print("\r\nWARNING: rescue elevation can fully modify the system.\r\n");
                                            print("Type: su enable\r\n");
                                            return;
                                        }

                                        if (!g_warnedSystemEnable)
                                        {
                                            g_warnedSystemEnable = true;
                                            print("\r\nWARNING: rescue elevation can fully modify the system.\r\n");
                                            print("Type: su enable\r\n");
                                            return;
                                        }

                                        g_role = QC::Cmd::AccessLevel::SysAdmin;
                                        print("\r\nrole=su\r\n");
                                    },
                                    "Enable su (SysAdmin) role (RECOVERY only; run twice)"});

                addCommandInternal({"system",
                                    [](int argc, const char *const *argv) {
                                        const auto mode = QK::Boot::Config::GetStartupMode();
                                        if (mode != QK::Boot::Config::StartupMode::Recovery)
                                        {
                                            print("\r\nsystem: only available in RECOVERY startup mode\r\n");
                                            return;
                                        }

                                        if (argc < 2 || !argv[1] || !streqIgnoreCase(argv[1], "enable"))
                                        {
                                            g_warnedSystemEnable = true;
                                            print("\r\nWARNING: system role has full control.\r\n");
                                            print("Type: system enable\r\n");
                                            return;
                                        }

                                        if (!g_warnedSystemEnable)
                                        {
                                            g_warnedSystemEnable = true;
                                            print("\r\nWARNING: system role has full control.\r\n");
                                            print("Type: system enable\r\n");
                                            return;
                                        }

                                        g_role = QC::Cmd::AccessLevel::System;
                                        print("\r\nrole=system\r\n");
                                    },
                                    "Enable system role (RECOVERY only; run twice)"});
            }

            void executeCommand()
            {
                // Keep an intact copy for registry execution; tokenizeInPlace mutates the buffer.
                char original[kBufferSize];
                QC::String::memset(original, 0, sizeof(original));
                QC::String::strncpy(original, g_buffer, sizeof(original) - 1);
                original[sizeof(original) - 1] = '\0';

                char *line = g_buffer;
                const char *argv[kMaxArgs];
                int argc = 0;
                if (!tokenizeInPlace(line, argv, argc))
                {
                    printPrompt();
                    return;
                }

                const Command *command = findCommand(argv[0]);
                if (command)
                {
                    command->handler(argc, argv);
                }
                else
                {
                    QK::CmdCenter::registerMvpCommands();
                    QC::Cmd::Context ctx;
                    ctx.out = [](const char *text, void *)
                    {
                        if (text && *text)
                            print(text);
                        print("\r\n");
                    };
                    ctx.callerAccess = g_role;
                    const bool handled = QC::Cmd::Registry::instance().execute(original, ctx);
                    if (!handled)
                        print("\r\nUnknown command\r\n");
                }

                printPrompt();
            }

        } // namespace

        void initialize(PrintFn printer)
        {
            g_printer = printer;
            clearBuffer();
            resetCommandTable();
            registerBuiltIns();
            QK::CmdCenter::registerMvpCommands();
            QK::CmdCenter::registerSystemVolumeCommands();
            QC::String::strncpy(g_cwd, "/", sizeof(g_cwd) - 1);
            g_cwd[sizeof(g_cwd) - 1] = '\0';
            QC::String::memset(g_transcript, 0, sizeof(g_transcript));
            g_transcriptLen = 0;
            g_transcriptTruncated = false;
            g_savetermLastSavedLen = 0;
            g_savetermHasBaseline = false;
            QC::String::memset(g_savetermLastPath, 0, sizeof(g_savetermLastPath));
            g_inputEnabled = false;
            g_role = QC::Cmd::AccessLevel::User;
            g_warnedAdminEnable = false;
            g_warnedSystemEnable = false;

            const QC::usize bootLogSize = QK::Boot::Log::Size();
            if (bootLogSize > 0)
            {
                char chunk[512];
                QC::usize offset = 0;
                while (offset < bootLogSize)
                {
                    const QC::usize copied = QK::Boot::Log::CopyOut(offset, chunk, sizeof(chunk) - 1);
                    if (copied == 0)
                        break;
                    chunk[copied] = '\0';
                    print(chunk);
                    offset += copied;
                }
            }

            print("\r\nCITADEL console ready\r\n");
        }

        void setRole(QC::Cmd::AccessLevel role)
        {
            g_role = role;
        }

        QC::Cmd::AccessLevel role()
        {
            return g_role;
        }

        void setInputEnabled(bool enabled)
        {
            g_inputEnabled = enabled;
        }

        bool inputEnabled()
        {
            return g_inputEnabled;
        }

        void setOwner(Owner owner)
        {
            g_owner = owner;
        }

        Owner owner()
        {
            return g_owner;
        }

        void setSafeFallbackEnabled(bool enabled)
        {
            g_safeFallbackEnabled = enabled;
        }

        bool safeFallbackEnabled()
        {
            return g_safeFallbackEnabled;
        }

        void handleKeyEvent(const QKDrv::PS2::KeyEvent &event)
        {
            if (!g_inputEnabled)
                return;

            if (!g_printer || !event.pressed)
                return;

            // Framebuffer scrollback controls (TERMINAL troubleshooting).
            if (event.key == QKDrv::PS2::Key::PageUp)
            {
                QK::Debug::FramebufferText::PageUp();
                return;
            }
            if (event.key == QKDrv::PS2::Key::PageDown)
            {
                QK::Debug::FramebufferText::PageDown();
                return;
            }

            // If the user starts typing while viewing history, snap back to the tail
            // so input doesn't occur off-screen.
            if (QK::Debug::FramebufferText::IsViewingHistory())
            {
                if (event.key == QKDrv::PS2::Key::Backspace || event.key == QKDrv::PS2::Key::Enter || (event.character >= 32 && event.character < 127))
                {
                    QK::Debug::FramebufferText::FollowTail();
                }
            }

            if (event.key == QKDrv::PS2::Key::Backspace)
            {
                backspace();
                return;
            }

            if (event.key == QKDrv::PS2::Key::Enter)
            {
                print("\r\n");

                if (g_captureActive)
                {
                    QC::String::strncpy(g_captureLine, g_buffer, sizeof(g_captureLine) - 1);
                    g_captureLine[sizeof(g_captureLine) - 1] = '\0';
                    clearBuffer();
                    g_captureReady = true;
                    return;
                }

                executeCommand();
                clearBuffer();
                return;
            }

            if (event.character >= 32 && event.character < 127)
            {
                appendChar(event.character);
            }
        }

        bool registerCommand(const Command &cmd)
        {
            return addCommandInternal(cmd);
        }

        void write(const char *msg)
        {
            print(msg);
        }

        void showPrompt()
        {
            if (g_owner != Console::Owner::TerminalOnly)
                return;
            printPrompt();
        }

        const char *cwd()
        {
            return g_cwd;
        }

        void executeLine(const char *line)
        {
            if (!line)
                return;

            if (!g_inputEnabled)
                return;

            clearBuffer();
            QC::String::strncpy(g_buffer, line, sizeof(g_buffer) - 1);
            g_buffer[sizeof(g_buffer) - 1] = '\0';
            executeCommand();
            clearBuffer();
        }

        bool readLineBlocking(char *out, QC::usize outSize, bool echo)
        {
            if (!out || outSize == 0)
                return false;
            if (!g_inputEnabled)
                return false;
            if (g_owner == Console::Owner::Desktop)
                return false;

            out[0] = '\0';

            const bool prevCaptureActive = g_captureActive;
            const bool prevCaptureEcho = g_captureEcho;

            g_captureActive = true;
            g_captureEcho = echo;
            g_captureReady = false;
            QC::String::memset(g_captureLine, 0, sizeof(g_captureLine));
            clearBuffer();

            while (!g_captureReady)
            {
                // USB keyboards are serviced by driver polling; without this,
                // pre-desktop blocking prompts can appear to ignore input.
                QKDrv::Manager::instance().poll();
                cpu_relax();
            }

            QC::String::strncpy(out, g_captureLine, outSize - 1);
            out[outSize - 1] = '\0';

            // best-effort wipe captured line
            QC::String::memset(g_captureLine, 0, sizeof(g_captureLine));

            g_captureActive = prevCaptureActive;
            g_captureEcho = prevCaptureEcho;
            g_captureReady = false;
            return true;
        }

    } // namespace Console

} // namespace QK
