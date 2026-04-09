#pragma once

// QKConsole - Minimal kernel console for interactive commands

#include "QCTypes.h"
#include "PS2/QKDrvPS2Keyboard.h"

#include "QCCommandRegistry.h"

namespace QK
{
    namespace Console
    {
        using PrintFn = void (*)(const char *msg);
        using CommandHandler = void (*)(int argc, const char *const *argv);

        struct Command
        {
            const char *name;
            CommandHandler handler;
            const char *description;
        };

        void initialize(PrintFn printer);
        // Enable/disable interactive input handling (keyboard -> console).
        // This does not affect Console::write() logging.
        void setInputEnabled(bool enabled);
        bool inputEnabled();
        void handleKeyEvent(const QKDrv::PS2::KeyEvent &event);
        bool registerCommand(const Command &cmd);

        // Executes a single command line (same parser/handlers as interactive input).
        // Note: does not synthesize per-character echo; it behaves like Enter was pressed.
        void executeLine(const char *line);

        // Reads a line of input (blocking) into out (NUL-terminated).
        // When echo is false, typed characters are not printed.
        // Returns false on invalid params.
        bool readLineBlocking(char *out, QC::usize outSize, bool echo = true);

        void write(const char *msg);
        const char *cwd();

        // Console command access role (used when executing QC::Cmd registry commands).
        void setRole(QC::Cmd::AccessLevel role);
        QC::Cmd::AccessLevel role();
    }
}
