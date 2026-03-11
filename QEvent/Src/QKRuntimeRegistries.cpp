#include "QKRuntimeRegistries.h"

#include "QCString.h"

namespace QK::Runtime
{
    Registries &Registries::instance()
    {
        static Registries r;
        return r;
    }

    void Registries::reset()
    {
        m_bootSeed = BootSeedConfig{};

        m_nextPid = 1;
        m_nextServiceId = 1;
        m_nextWindowSlotId = 1;
        m_nextResourceId = 1;

        for (QC::usize i = 0; i < MaxProcesses; ++i)
            m_processes[i] = ProcessRecord{};
        for (QC::usize i = 0; i < MaxServices; ++i)
            m_services[i] = ServiceRecord{};
        for (QC::usize i = 0; i < MaxWindows; ++i)
            m_windows[i] = WindowRecord{};
        for (QC::usize i = 0; i < MaxResources; ++i)
            m_resources[i] = ResourceRecord{};

        m_security = SecurityState{};
    }

    void Registries::rebuildFromBootSeed(const BootSeedConfig &seed)
    {
        // Deterministic reset + seed install.
        reset();
        m_bootSeed = seed;
    }

    QC::usize Registries::processCount() const
    {
        QC::usize n = 0;
        for (QC::usize i = 0; i < MaxProcesses; ++i)
        {
            if (m_processes[i].used)
                ++n;
        }
        return n;
    }

    QC::usize Registries::serviceCount() const
    {
        QC::usize n = 0;
        for (QC::usize i = 0; i < MaxServices; ++i)
        {
            if (m_services[i].used)
                ++n;
        }
        return n;
    }

    QC::usize Registries::windowCount() const
    {
        QC::usize n = 0;
        for (QC::usize i = 0; i < MaxWindows; ++i)
        {
            if (m_windows[i].used)
                ++n;
        }
        return n;
    }

    QC::usize Registries::resourceCount() const
    {
        QC::usize n = 0;
        for (QC::usize i = 0; i < MaxResources; ++i)
        {
            if (m_resources[i].used)
                ++n;
        }
        return n;
    }

    QC::usize Registries::copyWindowSnapshots(WindowSnapshot *out, QC::usize cap) const
    {
        if (!out || cap == 0)
            return 0;

        QC::usize outCount = 0;
        for (QC::usize i = 0; i < MaxWindows && outCount < cap; ++i)
        {
            if (!m_windows[i].used)
                continue;
            out[outCount++] = m_windows[i].snap;
        }
        return outCount;
    }

    static QC::usize findFreeSlotProcess(ProcessRecord recs[], QC::usize cap)
    {
        for (QC::usize i = 0; i < cap; ++i)
        {
            if (!recs[i].used)
                return i;
        }
        return cap;
    }

    QC::u32 Registries::createProcess(const ProcessRecord &recordSeed)
    {
        const QC::usize slot = findFreeSlotProcess(m_processes, MaxProcesses);
        if (slot == MaxProcesses)
            return 0;

        ProcessRecord rec = recordSeed;
        rec.used = true;
        rec.pid = (recordSeed.pid != 0) ? recordSeed.pid : m_nextPid++;
        m_processes[slot] = rec;
        return rec.pid;
    }

    bool Registries::updateProcess(QC::u32 pid, const ProcessRecord &record)
    {
        if (pid == 0)
            return false;
        for (QC::usize i = 0; i < MaxProcesses; ++i)
        {
            if (m_processes[i].used && m_processes[i].pid == pid)
            {
                ProcessRecord rec = record;
                rec.used = true;
                rec.pid = pid;
                m_processes[i] = rec;
                return true;
            }
        }
        return false;
    }

    bool Registries::destroyProcess(QC::u32 pid)
    {
        if (pid == 0)
            return false;
        for (QC::usize i = 0; i < MaxProcesses; ++i)
        {
            if (m_processes[i].used && m_processes[i].pid == pid)
            {
                m_processes[i] = ProcessRecord{};
                return true;
            }
        }
        return false;
    }

    const ProcessRecord *Registries::findProcess(QC::u32 pid) const
    {
        if (pid == 0)
            return nullptr;
        for (QC::usize i = 0; i < MaxProcesses; ++i)
        {
            if (m_processes[i].used && m_processes[i].pid == pid)
                return &m_processes[i];
        }
        return nullptr;
    }

    static QC::usize findFreeSlotService(ServiceRecord recs[], QC::usize cap)
    {
        for (QC::usize i = 0; i < cap; ++i)
        {
            if (!recs[i].used)
                return i;
        }
        return cap;
    }

    QC::u32 Registries::createService(const ServiceRecord &recordSeed)
    {
        const QC::usize slot = findFreeSlotService(m_services, MaxServices);
        if (slot == MaxServices)
            return 0;

        ServiceRecord rec = recordSeed;
        rec.used = true;
        rec.serviceId = (recordSeed.serviceId != 0) ? recordSeed.serviceId : m_nextServiceId++;
        m_services[slot] = rec;
        return rec.serviceId;
    }

    bool Registries::updateService(QC::u32 serviceId, const ServiceRecord &record)
    {
        if (serviceId == 0)
            return false;
        for (QC::usize i = 0; i < MaxServices; ++i)
        {
            if (m_services[i].used && m_services[i].serviceId == serviceId)
            {
                ServiceRecord rec = record;
                rec.used = true;
                rec.serviceId = serviceId;
                m_services[i] = rec;
                return true;
            }
        }
        return false;
    }

    bool Registries::destroyService(QC::u32 serviceId)
    {
        if (serviceId == 0)
            return false;
        for (QC::usize i = 0; i < MaxServices; ++i)
        {
            if (m_services[i].used && m_services[i].serviceId == serviceId)
            {
                m_services[i] = ServiceRecord{};
                return true;
            }
        }
        return false;
    }

    const ServiceRecord *Registries::findService(QC::u32 serviceId) const
    {
        if (serviceId == 0)
            return nullptr;
        for (QC::usize i = 0; i < MaxServices; ++i)
        {
            if (m_services[i].used && m_services[i].serviceId == serviceId)
                return &m_services[i];
        }
        return nullptr;
    }

    void Registries::clearWindows()
    {
        for (QC::usize i = 0; i < MaxWindows; ++i)
            m_windows[i] = WindowRecord{};
    }

    void Registries::syncWindows(const WindowSnapshot *snaps, QC::usize count)
    {
        clearWindows();

        if (!snaps || count == 0)
            return;

        if (count > MaxWindows)
            count = MaxWindows;

        for (QC::usize i = 0; i < count; ++i)
        {
            if (snaps[i].windowId == 0)
                continue;

            m_windows[i].used = true;
            m_windows[i].snap = snaps[i];
        }
    }

    const WindowRecord *Registries::findWindow(QC::u32 windowId) const
    {
        if (windowId == 0)
            return nullptr;
        for (QC::usize i = 0; i < MaxWindows; ++i)
        {
            if (m_windows[i].used && m_windows[i].snap.windowId == windowId)
                return &m_windows[i];
        }
        return nullptr;
    }

    static QC::usize findFreeSlotResource(ResourceRecord recs[], QC::usize cap)
    {
        for (QC::usize i = 0; i < cap; ++i)
        {
            if (!recs[i].used)
                return i;
        }
        return cap;
    }

    QC::u32 Registries::createResource(const ResourceRecord &recordSeed)
    {
        const QC::usize slot = findFreeSlotResource(m_resources, MaxResources);
        if (slot == MaxResources)
            return 0;

        ResourceRecord rec = recordSeed;
        rec.used = true;
        rec.resourceId = (recordSeed.resourceId != 0) ? recordSeed.resourceId : m_nextResourceId++;
        m_resources[slot] = rec;
        return rec.resourceId;
    }

    bool Registries::updateResource(QC::u32 resourceId, const ResourceRecord &record)
    {
        if (resourceId == 0)
            return false;
        for (QC::usize i = 0; i < MaxResources; ++i)
        {
            if (m_resources[i].used && m_resources[i].resourceId == resourceId)
            {
                ResourceRecord rec = record;
                rec.used = true;
                rec.resourceId = resourceId;
                m_resources[i] = rec;
                return true;
            }
        }
        return false;
    }

    bool Registries::destroyResource(QC::u32 resourceId)
    {
        if (resourceId == 0)
            return false;
        for (QC::usize i = 0; i < MaxResources; ++i)
        {
            if (m_resources[i].used && m_resources[i].resourceId == resourceId)
            {
                m_resources[i] = ResourceRecord{};
                return true;
            }
        }
        return false;
    }

    const ResourceRecord *Registries::findResource(QC::u32 resourceId) const
    {
        if (resourceId == 0)
            return nullptr;
        for (QC::usize i = 0; i < MaxResources; ++i)
        {
            if (m_resources[i].used && m_resources[i].resourceId == resourceId)
                return &m_resources[i];
        }
        return nullptr;
    }
}
