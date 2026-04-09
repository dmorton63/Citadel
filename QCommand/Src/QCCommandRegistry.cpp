#include "../Include/QCCommandRegistry.h"

#include "QCString.h"

namespace QC
{
    namespace Cmd
    {
        static inline char lowerAscii(char c)
        {
            if (c >= 'A' && c <= 'Z')
                return static_cast<char>(c + 32);
            return c;
        }

        Registry &Registry::instance()
        {
            static Registry reg;
            return reg;
        }

        Registry::Registry()
        {
            Entry e;
            e.name = "history";
            e.handler = &Registry::builtinHistoryHandler;
            e.userData = this;
            e.description = "Show command history (history [N])";
            e.access = AccessLevel::Everyone;
            m_entries.push_back(e);
        }

        const char *Registry::skipSpaces(const char *p)
        {
            while (p && (*p == ' ' || *p == '\t'))
                ++p;
            return p;
        }

        bool Registry::equalsIgnoreCase(const char *a, const char *b)
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

        bool Registry::parseUsize(const char *text, QC::usize &out)
        {
            out = 0;
            if (!text)
                return false;

            const char *p = skipSpaces(text);
            if (!p || *p == '\0')
                return false;

            QC::usize value = 0;
            bool any = false;
            while (*p >= '0' && *p <= '9')
            {
                any = true;
                const QC::usize digit = static_cast<QC::usize>(*p - '0');
                value = value * 10 + digit;
                ++p;
            }

            p = skipSpaces(p);
            if (!any || (p && *p != '\0'))
                return false;

            out = value;
            return true;
        }

        void Registry::writeU64(char *out, QC::usize outSize, QC::u64 value)
        {
            if (!out || outSize == 0)
                return;

            char tmp[32];
            QC::usize n = 0;
            do
            {
                const QC::u64 digit = value % 10;
                tmp[n++] = static_cast<char>('0' + digit);
                value /= 10;
            } while (value != 0 && n < sizeof(tmp));

            QC::usize oi = 0;
            while (n > 0 && oi + 1 < outSize)
                out[oi++] = tmp[--n];
            out[oi] = '\0';
        }

        QC::u8 Registry::countArgs(const char *args)
        {
            if (!args)
                return 0;

            QC::u8 count = 0;
            const char *p = skipSpaces(args);
            while (p && *p)
            {
                ++count;
                bool inQuote = false;
                while (*p)
                {
                    if (*p == '"')
                    {
                        inQuote = !inQuote;
                        ++p;
                        continue;
                    }

                    if (!inQuote && (*p == ' ' || *p == '\t'))
                        break;

                    // Treat backslash escapes inside quoted values as one logical char.
                    if (inQuote && *p == '\\' && p[1])
                    {
                        p += 2;
                        continue;
                    }

                    ++p;
                }

                p = skipSpaces(p);
            }
            return count;
        }

        void Registry::recordHistoryLine(const char *line)
        {
            if (!line)
                return;

            const QC::usize slot = static_cast<QC::usize>(m_historyTotal % kHistoryCapacity);
            QC::String::memset(m_history[slot], 0, kHistoryLineSize);
            QC::String::strncpy(m_history[slot], line, kHistoryLineSize - 1);
            m_history[slot][kHistoryLineSize - 1] = '\0';
            ++m_historyTotal;
        }

        bool Registry::resolveHistoryRecall(const char *line, char *out, QC::usize outSize, const Context &ctx) const
        {
            if (!line || !out || outSize == 0)
                return false;

            const char *p = skipSpaces(line);
            if (!p || *p != '!')
                return false;

            ++p;
            if (*p < '0' || *p > '9')
            {
                ctx.writeLine("history: usage !<index>");
                out[0] = '\0';
                return true;
            }

            QC::u64 index = 0;
            while (*p >= '0' && *p <= '9')
            {
                index = index * 10 + static_cast<QC::u64>(*p - '0');
                ++p;
            }

            p = skipSpaces(p);
            if (p && *p != '\0')
            {
                ctx.writeLine("history: usage !<index>");
                out[0] = '\0';
                return true;
            }

            if (m_historyTotal == 0)
            {
                ctx.writeLine("history: empty");
                out[0] = '\0';
                return true;
            }

            const QC::u64 visible = (m_historyTotal < kHistoryCapacity) ? m_historyTotal : static_cast<QC::u64>(kHistoryCapacity);
            const QC::u64 first = m_historyTotal - visible + 1;
            if (index < first || index > m_historyTotal)
            {
                ctx.writeLine("history: index not found");
                out[0] = '\0';
                return true;
            }

            const QC::usize slot = static_cast<QC::usize>((index - 1) % kHistoryCapacity);
            QC::String::strncpy(out, m_history[slot], outSize - 1);
            out[outSize - 1] = '\0';
            return true;
        }

        bool Registry::builtinHistoryHandler(const char *args, const Context &ctx, void *userData)
        {
            Registry *self = static_cast<Registry *>(userData);
            if (!self)
                return true;
            return self->handleHistoryCommand(args, ctx);
        }

        bool Registry::handleHistoryCommand(const char *args, const Context &ctx) const
        {
            if (m_historyTotal == 0)
            {
                ctx.writeLine("history: empty");
                return true;
            }

            QC::usize limit = 20;
            if (args)
            {
                const char *trimmed = skipSpaces(args);
                if (trimmed && *trimmed)
                {
                    if (!parseUsize(trimmed, limit) || limit == 0)
                    {
                        ctx.writeLine("usage: history [N]");
                        return true;
                    }
                }
            }

            const QC::u64 visible = (m_historyTotal < kHistoryCapacity) ? m_historyTotal : static_cast<QC::u64>(kHistoryCapacity);
            QC::u64 show = static_cast<QC::u64>(limit);
            if (show > visible)
                show = visible;

            const QC::u64 first = m_historyTotal - visible + 1;
            const QC::u64 start = m_historyTotal - show + 1;

            char line[256];
            for (QC::u64 i = start; i <= m_historyTotal; ++i)
            {
                const QC::usize slot = static_cast<QC::usize>((i - 1) % kHistoryCapacity);
                QC::String::memset(line, 0, sizeof(line));

                char number[32];
                QC::String::memset(number, 0, sizeof(number));
                writeU64(number, sizeof(number), i);

                QC::String::strncpy(line, number, sizeof(line) - 1);
                const QC::usize used = QC::String::strlen(line);
                if (used + 2 < sizeof(line))
                {
                    line[used] = ':';
                    line[used + 1] = ' ';
                    line[used + 2] = '\0';
                }

                const QC::usize used2 = QC::String::strlen(line);
                if (used2 + 1 < sizeof(line))
                {
                    QC::String::strncpy(line + used2, m_history[slot], sizeof(line) - 1 - used2);
                    line[sizeof(line) - 1] = '\0';
                }

                ctx.writeLine(line);
            }

            if (first > 1)
                ctx.writeLine("history: older entries evicted");
            return true;
        }

        bool Registry::registerCommand(const char *name, Handler handler, void *userData)
        {
            return registerCommandEx(name, handler, userData, nullptr);
        }

        bool Registry::registerCommandEx(const char *name, Handler handler, void *userData, const char *description)
        {
            return registerCommandExAccess(name, AccessLevel::Everyone, handler, userData, description);
        }

        bool Registry::registerCommandExAccess(const char *name, AccessLevel access, Handler handler, void *userData, const char *description)
        {
            if (!name || !handler)
                return false;

            // Reject duplicates.
            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                if (m_entries[i].name && equalsIgnoreCase(m_entries[i].name, name))
                    return false;
            }

            Entry e;
            e.name = name;
            e.handler = handler;
            e.userData = userData;
            e.description = description;
            e.usage = nullptr;
            e.argSchema = nullptr;
            e.access = access;
            e.minArgs = 0;
            e.maxArgs = 0;
            e.enforceArgCount = false;
            m_entries.push_back(e);
            return true;
        }

        bool Registry::setCommandMetadata(const char *name,
                                          const char *usage,
                                          const char *argSchema,
                                          QC::u8 minArgs,
                                          QC::u8 maxArgs,
                                          bool enforceArgCount)
        {
            if (!name || !*name)
                return false;
            if (enforceArgCount && minArgs > maxArgs)
                return false;

            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                Entry &e = m_entries[i];
                if (!e.name)
                    continue;
                if (!equalsIgnoreCase(e.name, name))
                    continue;

                e.usage = usage;
                e.argSchema = argSchema;
                e.minArgs = minArgs;
                e.maxArgs = maxArgs;
                e.enforceArgCount = enforceArgCount;
                return true;
            }

            return false;
        }

        const Registry::AliasEntry *Registry::findAlias(const char *name) const
        {
            if (!name || !*name)
                return nullptr;
            for (QC::usize i = 0; i < m_aliases.size(); ++i)
            {
                if (equalsIgnoreCase(m_aliases[i].alias, name))
                    return &m_aliases[i];
            }
            return nullptr;
        }

        Registry::AliasEntry *Registry::findAliasMutable(const char *name)
        {
            if (!name || !*name)
                return nullptr;
            for (QC::usize i = 0; i < m_aliases.size(); ++i)
            {
                if (equalsIgnoreCase(m_aliases[i].alias, name))
                    return &m_aliases[i];
            }
            return nullptr;
        }

        bool Registry::registerAlias(const char *alias, const char *expansion, bool replaceExisting)
        {
            if (!alias || !*alias || !expansion || !*expansion)
                return false;

            AliasEntry *existing = findAliasMutable(alias);
            if (existing)
            {
                if (!replaceExisting)
                    return false;
                QC::String::memset(existing->expansion, 0, sizeof(existing->expansion));
                QC::String::strncpy(existing->expansion, expansion, sizeof(existing->expansion) - 1);
                existing->expansion[sizeof(existing->expansion) - 1] = '\0';
                return true;
            }

            AliasEntry a;
            QC::String::memset(&a, 0, sizeof(a));
            QC::String::strncpy(a.alias, alias, sizeof(a.alias) - 1);
            a.alias[sizeof(a.alias) - 1] = '\0';
            QC::String::strncpy(a.expansion, expansion, sizeof(a.expansion) - 1);
            a.expansion[sizeof(a.expansion) - 1] = '\0';
            m_aliases.push_back(a);
            return true;
        }

        bool Registry::removeAlias(const char *alias)
        {
            if (!alias || !*alias)
                return false;

            for (QC::usize i = 0; i < m_aliases.size(); ++i)
            {
                if (!equalsIgnoreCase(m_aliases[i].alias, alias))
                    continue;

                for (QC::usize j = i + 1; j < m_aliases.size(); ++j)
                    m_aliases[j - 1] = m_aliases[j];
                if (!m_aliases.empty())
                    m_aliases.pop_back();
                return true;
            }
            return false;
        }

        const char *Registry::aliasNameAt(QC::usize index) const
        {
            if (index >= m_aliases.size())
                return nullptr;
            return m_aliases[index].alias;
        }

        const char *Registry::aliasExpansionAt(QC::usize index) const
        {
            if (index >= m_aliases.size())
                return nullptr;
            return m_aliases[index].expansion;
        }

        const char *Registry::commandNameAt(QC::usize index) const
        {
            if (index >= m_entries.size())
                return nullptr;
            return m_entries[index].name;
        }

        const char *Registry::commandDescriptionAt(QC::usize index) const
        {
            if (index >= m_entries.size())
                return nullptr;
            return m_entries[index].description;
        }

        const char *Registry::commandUsageAt(QC::usize index) const
        {
            if (index >= m_entries.size())
                return nullptr;
            return m_entries[index].usage;
        }

        const char *Registry::commandArgSchemaAt(QC::usize index) const
        {
            if (index >= m_entries.size())
                return nullptr;
            return m_entries[index].argSchema;
        }

        AccessLevel Registry::commandAccessAt(QC::usize index) const
        {
            if (index >= m_entries.size())
                return AccessLevel::Everyone;
            return m_entries[index].access;
        }

        const char *Registry::findDescription(const char *name) const
        {
            if (!name)
                return nullptr;

            for (QC::usize i = 0; i < m_entries.size(); ++i)
            {
                const Entry &e = m_entries[i];
                if (!e.name)
                    continue;
                if (equalsIgnoreCase(e.name, name))
                    return e.description;
            }

            return nullptr;
        }

        bool Registry::execute(const char *line, const Context &ctx)
        {
            if (!line)
                return false;

            ++m_executionCount;

            const char *p = skipSpaces(line);
            if (*p == '\0')
                return false;

            char effective[256];
            QC::String::memset(effective, 0, sizeof(effective));
            QC::String::strncpy(effective, p, sizeof(effective) - 1);
            effective[sizeof(effective) - 1] = '\0';

            char recalled[256];
            QC::String::memset(recalled, 0, sizeof(recalled));
            const bool recallHandled = resolveHistoryRecall(effective, recalled, sizeof(recalled), ctx);
            if (recallHandled)
            {
                if (recalled[0] == '\0')
                    return true;
                QC::String::strncpy(effective, recalled, sizeof(effective) - 1);
                effective[sizeof(effective) - 1] = '\0';
            }

            recordHistoryLine(effective);

            char working[256];
            QC::String::memset(working, 0, sizeof(working));
            QC::String::strncpy(working, effective, sizeof(working) - 1);
            working[sizeof(working) - 1] = '\0';

            for (QC::usize depth = 0; depth < 4; ++depth)
            {
                p = skipSpaces(working);

                // Extract command token.
                char cmd[48];
                QC::usize ci = 0;
                while (*p && *p != ' ' && *p != '\t' && ci + 1 < sizeof(cmd))
                {
                    cmd[ci++] = *p++;
                }
                cmd[ci] = '\0';

                p = skipSpaces(p);

                const AliasEntry *alias = findAlias(cmd);
                if (alias)
                {
                    char expanded[256];
                    QC::String::memset(expanded, 0, sizeof(expanded));
                    QC::String::strncpy(expanded, alias->expansion, sizeof(expanded) - 1);
                    expanded[sizeof(expanded) - 1] = '\0';
                    if (p && *p)
                    {
                        const QC::usize used = QC::String::strlen(expanded);
                        if (used + 1 < sizeof(expanded))
                        {
                            expanded[used] = ' ';
                            expanded[used + 1] = '\0';
                            QC::String::strncpy(expanded + used + 1, p, sizeof(expanded) - used - 2);
                            expanded[sizeof(expanded) - 1] = '\0';
                        }
                    }
                    QC::String::strncpy(working, expanded, sizeof(working) - 1);
                    working[sizeof(working) - 1] = '\0';
                    continue;
                }

                for (QC::usize i = 0; i < m_entries.size(); ++i)
                {
                    const Entry &e = m_entries[i];
                    if (!e.name || !e.handler)
                        continue;

                    if (equalsIgnoreCase(e.name, cmd))
                    {
                        if (static_cast<QC::u8>(ctx.callerAccess) < static_cast<QC::u8>(e.access))
                        {
                            ctx.writeLine("Permission denied");
                            return true;
                        }

                        if (e.enforceArgCount)
                        {
                            // Parse guard: reject obviously malformed quote usage before counting args.
                            bool quoteOpen = false;
                            for (const char *q = p; q && *q; ++q)
                            {
                                if (*q == '"')
                                    quoteOpen = !quoteOpen;
                                if (quoteOpen && *q == '\\' && q[1])
                                    ++q;
                            }
                            if (quoteOpen)
                            {
                                ++m_parseErrorCount;
                                ctx.writeLine("parse error: unclosed quote");
                                return true;
                            }

                            const QC::u8 argc = countArgs(p);
                            if (argc < e.minArgs || argc > e.maxArgs)
                            {
                                ++m_parseErrorCount;
                                if (e.usage && *e.usage)
                                {
                                    char line[192];
                                    QC::String::memset(line, 0, sizeof(line));
                                    QC::String::strncpy(line, "usage: ", sizeof(line) - 1);
                                    const QC::usize used = QC::String::strlen(line);
                                    QC::String::strncpy(line + used, e.usage, sizeof(line) - used - 1);
                                    line[sizeof(line) - 1] = '\0';
                                    ctx.writeLine(line);
                                }
                                else
                                {
                                    ctx.writeLine("invalid arguments");
                                }
                                return true;
                            }
                        }

                        return e.handler(p, ctx, e.userData);
                    }
                }

                return false;
            }

            ctx.writeLine("alias: expansion loop detected");
            return true;

        }

    } // namespace Cmd
} // namespace QC
