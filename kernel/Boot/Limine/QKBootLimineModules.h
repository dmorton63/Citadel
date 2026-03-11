#pragma once

#include "QCTypes.h"

#define LIMINE_API_REVISION 2
#include "limine.h"

namespace QK::Boot::Limine
{
    const limine_module_response *GetModuleResponse(QC::u64 ModuleRequest[]);
    const limine_file *FindRamdiskModule(QC::u64 ModuleRequest[]);
}
