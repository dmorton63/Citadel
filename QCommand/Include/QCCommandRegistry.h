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

            // Execute a command line. Returns true if a command was found and invoked.
            bool execute(const char *line, const Context &ctx);

            // Best-effort enumeration for help output.
            QC::usize commandCount() const { return m_entries.size(); }
            const char *commandNameAt(QC::usize index) const;
            const char *commandDescriptionAt(QC::usize index) const;
            AccessLevel commandAccessAt(QC::usize index) const;

            // Best-effort lookup for help output.
            const char *findDescription(const char *name) const;

        private:
            Registry() = default;

            struct Entry
            {
                const char *name = nullptr;
                Handler handler = nullptr;
                void *userData = nullptr;
                const char *description = nullptr;
                AccessLevel access = AccessLevel::Everyone;
            };

            QC::Vector<Entry> m_entries;

            static bool equalsIgnoreCase(const char *a, const char *b);
            static const char *skipSpaces(const char *p);
        };

    } // namespace Cmd
} // namespace QC
