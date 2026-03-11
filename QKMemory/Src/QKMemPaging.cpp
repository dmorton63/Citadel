// QKMemory Paging - Implementation
// Namespace: QK::Memory

#include "QKMemPaging.h"
#include "QKMemPMM.h"
#include "QKMemTranslator.h"
#include "QCLogger.h"
#include "QCString.h"

namespace QK::Memory
{

    Paging &Paging::instance()
    {
        static Paging instance;
        return instance;
    }

    Paging::Paging()
    {
    }

    Paging::~Paging()
    {
    }

    void Paging::initialize()
    {
        QC_LOG_INFO("QKMemPaging", "Initializing paging subsystem");
        // Paging is typically already enabled by bootloader
        QC_LOG_INFO("QKMemPaging", "Paging subsystem initialized");
    }

    PageTable *Paging::createPageTable()
    {
        QC::PhysAddr phys = PMM::instance().allocatePage();
        if (phys == 0)
            return nullptr;

        PageTable *table = phys_to_virt<PageTable>(phys);
        QC::String::memset(table, 0, sizeof(PageTable));

        return table;
    }

    void Paging::freePageTable(PageTable *table)
    {
        if (!table)
            return;
        QC::PhysAddr phys = virt_to_phys(reinterpret_cast<QC::VirtAddr>(table));
        PMM::instance().freePage(phys);
    }

    void Paging::flushTLB()
    {
        QC::PhysAddr cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    }

    void Paging::flushPage(QC::VirtAddr addr)
    {
        asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
    }

    void Paging::flushRange(QC::VirtAddr start, QC::usize size)
    {
        QC::usize pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        for (QC::usize i = 0; i < pages; ++i)
        {
            flushPage(start + i * PAGE_SIZE);
        }
    }

} // namespace QK::Memory
