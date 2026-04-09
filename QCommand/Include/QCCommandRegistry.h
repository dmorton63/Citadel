#pragma once

// QCCommandRegistry - shared command registration + dispatch
// Namespace: QC::Cmd
//
// Design:
// - Freestanding-friendly (no STL/exceptions)
// - Command handlers stream output via a callback
// - Registry is shared so multiple front-ends (terminal, command processor, etc.) can reuse the same commands

#include "QCTypes.h"
#include "QCVector.h"

namespace QC
{
    namespace Cmd
    {
        using OutputFn = void (*)(const char *text, void *userData);

        enum class AccessLevel : QC::u8
        {
            Everyone = 0,
            User = 1,
            Admin = 2,
            SysAdmin = 3,
            System = 4,
        };

        struct Context
        {
            OutputFn out = nullptr;
            void *userData = nullptr;
            AccessLevel callerAccess = AccessLevel::Everyone;

            void writeLine(const char *text) const
            {
                if (out)
                    out(text, userData);
            }
        };

        using Handler = bool (*)(const char *args, const Context &ctx, void *userData);

        class Registry
        {
        public:
            static Registry &instance();

            bool registerCommand(const char *name, Handler handler, void *userData = nullptr);

            // Extended registration with an optional description (used by help output).
            bool registerCommandEx(const char *name, Handler handler, void *userData, const char *description);

            // Like registerCommandEx, but allows tagging a command with an access level.
            // Note: this is metadata-only today; enforcement is expected to be layered into execute().
            bool registerCommandExAccess(const char *name, AccessLevel access, Handler handler, void *userData, const char *description);

            // Attach usage/schema metadata and optional argument-count validation.
            bool setCommandMetadata(const char *name,
                                    const char *usage,
                                    const char *argSchema,
                                    QC::u8 minArgs,
                                    QC::u8 maxArgs,
                                    bool enforceArgCount = true);

            // Execute a command line. Returns true if a command was found and invoked.
            bool execute(const char *line, const Context &ctx);

            // Lightweight execution/parser telemetry for diagnostics.
            QC::u64 executionCount() const { return m_executionCount; }
            QC::u64 parseErrorCount() const { return m_parseErrorCount; }

            // Best-effort enumeration for help output.
            QC::usize commandCount() const { return m_entries.size(); }
            const char *commandNameAt(QC::usize index) const;
            const char *commandDescriptionAt(QC::usize index) const;
            const char *commandUsageAt(QC::usize index) const;
            const char *commandArgSchemaAt(QC::usize index) const;
            AccessLevel commandAccessAt(QC::usize index) const;

            // Best-effort lookup for help output.
            const char *findDescription(const char *name) const;

            // Alias map support so command behavior can be extended from persisted config.
            bool registerAlias(const char *alias, const char *expansion, bool replaceExisting = true);
            bool removeAlias(const char *alias);
            QC::usize aliasCount() const { return m_aliases.size(); }
            const char *aliasNameAt(QC::usize index) const;
            const char *aliasExpansionAt(QC::usize index) const;

        private:
            Registry();

            struct Entry
            {
                const char *name = nullptr;
                Handler handler = nullptr;
                void *userData = nullptr;
                const char *description = nullptr;
                const char *usage = nullptr;
                const char *argSchema = nullptr;
                AccessLevel access = AccessLevel::Everyone;
                QC::u8 minArgs = 0;
                QC::u8 maxArgs = 0;
                bool enforceArgCount = false;
            };

            struct AliasEntry
            {
                char alias[48]{};
                char expansion[192]{};
            };

            static constexpr QC::usize kHistoryCapacity = 128;
            static constexpr QC::usize kHistoryLineSize = 192;

            QC::Vector<Entry> m_entries;
            QC::Vector<AliasEntry> m_aliases;
            char m_history[kHistoryCapacity][kHistoryLineSize]{};
            QC::u64 m_historyTotal = 0;
            QC::u64 m_executionCount = 0;
            QC::u64 m_parseErrorCount = 0;

            static bool equalsIgnoreCase(const char *a, const char *b);
            static const char *skipSpaces(const char *p);

            static bool parseUsize(const char *text, QC::usize &out);
            static void writeU64(char *out, QC::usize outSize, QC::u64 value);
            static QC::u8 countArgs(const char *args);

            void recordHistoryLine(const char *line);
            bool resolveHistoryRecall(const char *line, char *out, QC::usize outSize, const Context &ctx) const;
            const AliasEntry *findAlias(const char *name) const;
            AliasEntry *findAliasMutable(const char *name);

            static bool builtinHistoryHandler(const char *args, const Context &ctx, void *userData);
            bool handleHistoryCommand(const char *args, const Context &ctx) const;
        };

    } // namespace Cmd
} // namespace QC
