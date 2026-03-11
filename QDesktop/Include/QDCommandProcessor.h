#pragma once

// QDesktop CommandProcessor - service that executes shared commands
// Namespace: QD

#include "QCTypes.h"

namespace QK
{
    namespace Msg
    {
        struct Envelope;
    }
}

namespace QD
{
    class CommandProcessor
    {
    public:
        static CommandProcessor &instance();

        void initialize();

    private:
        CommandProcessor() = default;

        static void onServiceMessage(QK::Msg::Envelope *env, void *userData);

        bool m_initialized = false;
        QC::u32 m_serviceId = 0;
        bool m_commandsRegistered = false;

        void registerCommands();
    };

}
