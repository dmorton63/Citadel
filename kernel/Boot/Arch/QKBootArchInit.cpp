#include "QKBootArchInit.h"

#include "QArchCPU.h"
#include "QArchGDT.h"
#include "QArchIDT.h"
#include "QKInterrupts.h"

namespace QK::Boot::Arch
{
    void InitCpuGdtIdtAndInterrupts(FLogFn Log)
    {
        if (Log)
        {
            Log("About to call CPU init\r\n");
        }

        QArch::CPU::instance().initialize();
        if (Log)
        {
            Log("CPU initialized\r\n");
        }

        QArch::GDT::instance().initialize();
        if (Log)
        {
            Log("GDT initialized\r\n");
        }

        QArch::IDT::instance().initialize();
        if (Log)
        {
            Log("IDT initialized\r\n");
        }

        QK::InterruptManager::instance().initialize();
        if (Log)
        {
            Log("InterruptManager initialized\r\n");
        }
    }
}
