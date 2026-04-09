#pragma once

#include "QCTypes.h"
#include "QCVector.h"

namespace QK::Module
{
    struct LoadedModule
    {
        char id[32] = {0};
        char path[192] = {0};
        QC::u64 bytes = 0;
        QC::u8 depCount = 0;
    };

    struct DependencyEdge
    {
        char from[32] = {0};
        char to[32] = {0};
    };

    struct FetchReport
    {
        QC::u32 loadedModules = 0;
        QC::u64 loadedBytes = 0;
        QC::u32 parkedModules = 0;
        QC::u32 quarantinedModules = 0;
    };

    struct InspectionState
    {
        bool allowed = false;
        char detail[96] = {0};
        char quarantinePath[192] = {0};
        char parkedPath[192] = {0};
    };

    class Loader
    {
    public:
        static Loader &instance();

        QC::Status loadCatalog(const char *catalogPath = "/system/modules/MODULES.CFG");
        QC::Status load(const char *moduleId, FetchReport *outReport = nullptr);
        QC::Status loadSandboxed(const char *moduleId, FetchReport *outReport = nullptr);
        QC::Status fetchWithDependencies(const char *moduleId, FetchReport *outReport = nullptr);
        QC::Status unload(const char *moduleId);
        QC::usize listLoaded(LoadedModule *outModules, QC::usize cap);
        QC::usize buildDependencyGraph(const char *moduleId, DependencyEdge *outEdges, QC::usize edgeCap);
        QC::Status lastInspectionState(InspectionState &out) const;

    private:
        Loader() = default;
    };
}
