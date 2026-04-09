#pragma once

#include "QCTypes.h"

namespace QK::Runtime
{
    // Tier kind is intentionally decoupled from boot/config headers.
    enum class TierKind : QC::u8
    {
        Unknown = 0,
        Production = 1,
        Golden = 2,
    };

    struct BootSeedModule
    {
        char id[32] = {0};
        char type[32] = {0};
        char resolvedPath[320] = {0};
        bool required = false;
        bool hasJson = false;

        // Layer-2 module trust metadata
        char role[16] = {0};
        char status[16] = {0};
        bool hashRequired = false;
        bool signatureRequired = false;
    };

    struct BootSeedConfig
    {
        TierKind tier = TierKind::Unknown;
        char tierName[16] = {0};
        char tierRoot[160] = {0};

        QC::u32 moduleCount = 0;
        BootSeedModule modules[16] = {};
    };

    enum class ProcessState : QC::u8
    {
        Unknown = 0,
        Starting,
        Running,
        Blocked,
        Exiting,
        Exited,
    };

    struct ProcessRecord
    {
        bool used = false;
        QC::u32 pid = 0;
        QC::u32 parentPid = 0;
        ProcessState state = ProcessState::Unknown;
        QC::u32 imageId = 0;     // opaque identity (hash or monotonic id)
        QC::u32 sandboxId = 0;   // opaque sandbox identity
        QC::u64 capabilities = 0; // bitmask
    };

    enum class ServiceDesiredState : QC::u8
    {
        Stopped = 0,
        Running = 1,
    };

    struct ServiceRecord
    {
        bool used = false;
        QC::u32 serviceId = 0;
        char name[48] = {0};
        ServiceDesiredState desired = ServiceDesiredState::Stopped;
        QC::u64 capabilities = 0; // bitmask

        QC::u32 depCount = 0;
        QC::u32 deps[8] = {}; // serviceIds

        char entrypoint[96] = {0}; // debug/metadata; not an execution contract yet
    };

    struct WindowSnapshot
    {
        QC::u32 windowId = 0;
        QC::i32 x = 0;
        QC::i32 y = 0;
        QC::u32 width = 0;
        QC::u32 height = 0;
        QC::u32 flags = 0;
        QC::u32 zIndex = 0;
        bool focused = false;

        QC::u32 ownerPid = 0;
    };

    struct WindowRecord
    {
        bool used = false;
        WindowSnapshot snap{};
    };

    enum class ResourceType : QC::u8
    {
        Unknown = 0,
        Texture,
        Font,
        Model,
        Audio,
        Blob,
    };

    struct ResourceRecord
    {
        bool used = false;
        QC::u32 resourceId = 0;
        ResourceType type = ResourceType::Unknown;
        QC::u32 ownerPid = 0;
        QC::u32 refCount = 0;
        bool resident = false;
    };

    struct SecurityState
    {
        bool tpmAvailable = false;
        bool enforcementEnabled = false;
        QC::u32 measuredArtifactCount = 0; // summary-only for now

        // SC runtime memory hardening markers (MVP policy surface).
        bool scNoSwap = false;
        bool scNoDump = false;
        bool scMinimalExposure = false;

        // General internal execution/data hardening markers.
        bool guardedExecutionEnabled = false;
        bool protectedAppExecutionSpace = false;
        bool hiddenEncryptedScStorage = false;
        bool scInternalOnly = false;
        bool scBackgroundSystemTask = false;
    };

    enum class PortProtocol : QC::u8
    {
        Unknown = 0,
        UDP = 1,
        TCP = 2,
    };

    struct PortRecord
    {
        bool used = false;
        PortProtocol protocol = PortProtocol::Unknown;
        QC::u16 port = 0;
        QC::u32 ownerPid = 0;
    };

    // Kernel-owned runtime registry hub.
    //
    // MVP intent:
    // - deterministic reset + repopulate during boot commit
    // - stable IDs within a boot session (monotonic, no reuse)
    // - no persistence and no user access
    class Registries
    {
    public:
        static Registries &instance();

        static constexpr QC::usize MaxProcesses = 128;
        static constexpr QC::usize MaxServices = 64;
        static constexpr QC::usize MaxWindows = 128;
        static constexpr QC::usize MaxResources = 256;
        static constexpr QC::usize MaxPorts = 256;

        void reset();

        // Boot-time atomic seed/commit.
        void rebuildFromBootSeed(const BootSeedConfig &seed);
        const BootSeedConfig &bootSeed() const { return m_bootSeed; }

        // Debug helpers
        void dumpBootSeedModules(void (*log)(const char *)) const;

        QC::usize processCount() const;
        QC::usize serviceCount() const;
        QC::usize windowCount() const;
        QC::usize resourceCount() const;
        QC::usize portCount() const;

        // Copies up to cap window snapshots into out.
        // Returns number of snapshots copied.
        QC::usize copyWindowSnapshots(WindowSnapshot *out, QC::usize cap) const;

        // Process registry (MVP)
        QC::u32 createProcess(const ProcessRecord &recordSeed);
        bool updateProcess(QC::u32 pid, const ProcessRecord &record);
        bool destroyProcess(QC::u32 pid);
        const ProcessRecord *findProcess(QC::u32 pid) const;

        // Service registry (MVP)
        QC::u32 createService(const ServiceRecord &recordSeed);
        bool updateService(QC::u32 serviceId, const ServiceRecord &record);
        bool destroyService(QC::u32 serviceId);
        const ServiceRecord *findService(QC::u32 serviceId) const;

        // Window registry (synced from WindowManager)
        void syncWindows(const WindowSnapshot *snaps, QC::usize count);
        const WindowRecord *findWindow(QC::u32 windowId) const;

        // Resource registry (MVP)
        QC::u32 createResource(const ResourceRecord &recordSeed);
        bool updateResource(QC::u32 resourceId, const ResourceRecord &record);
        bool destroyResource(QC::u32 resourceId);
        const ResourceRecord *findResource(QC::u32 resourceId) const;

        // Security registry (MVP)
        void setSecurityState(const SecurityState &state) { m_security = state; }
        const SecurityState &securityState() const { return m_security; }

        // Port ownership registry (MVP)
        bool registerPort(PortProtocol protocol, QC::u16 port, QC::u32 ownerPid);
        bool unregisterPort(PortProtocol protocol, QC::u16 port);
        const PortRecord *findPort(PortProtocol protocol, QC::u16 port) const;

    private:
        Registries() = default;

        BootSeedConfig m_bootSeed{};

        QC::u32 m_nextPid = 1;
        QC::u32 m_nextServiceId = 1;
        QC::u32 m_nextWindowSlotId = 1; // unused today; reserved
        QC::u32 m_nextResourceId = 1;

        ProcessRecord m_processes[MaxProcesses] = {};
        ServiceRecord m_services[MaxServices] = {};
        WindowRecord m_windows[MaxWindows] = {};
        ResourceRecord m_resources[MaxResources] = {};
        PortRecord m_ports[MaxPorts] = {};

        SecurityState m_security{};

        void clearWindows();
    };
}
