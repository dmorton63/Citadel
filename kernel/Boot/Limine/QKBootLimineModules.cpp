#include "QKBootLimineModules.h"

#include "QCString.h"

namespace QK::Boot::Limine
{
    const limine_module_response *GetModuleResponse(QC::u64 ModuleRequest[])
    {
        if (!ModuleRequest)
        {
            return nullptr;
        }

        return reinterpret_cast<const limine_module_response *>(ModuleRequest[5]);
    }

    const limine_file *FindRamdiskModule(QC::u64 ModuleRequest[])
    {
        const limine_module_response *Response = GetModuleResponse(ModuleRequest);
        if (!Response || Response->module_count == 0 || !Response->modules)
        {
            return nullptr;
        }

        const limine_file *Fallback = nullptr;
        for (QC::u64 i = 0; i < Response->module_count; ++i)
        {
            const limine_file *Candidate = Response->modules[i];
            if (!Candidate)
            {
                continue;
            }

            if (Candidate->cmdline && QC::String::strcmp(Candidate->cmdline, "ramdisk") == 0)
            {
                return Candidate;
            }

            if (!Fallback)
            {
                Fallback = Candidate;
            }
        }

        return Fallback;
    }
}
