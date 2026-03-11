#pragma once

// QArch GDT - Global Descriptor Table
// Namespace: QArch

#include "QCTypes.h"

namespace QArch
{

    struct GDTEntry
    {
        QC::u16 limitLow;
        QC::u16 baseLow;
        QC::u8 baseMiddle;
        QC::u8 access;
        QC::u8 granularity;
        QC::u8 baseHigh;
    } __attribute__((packed));

    struct GDTEntry64 : public GDTEntry
    {
        QC::u32 baseUpper;
        QC::u32 reserved;
    } __attribute__((packed));

    struct GDTPointer
    {
        QC::u16 limit;
        QC::u64 base;
    } __attribute__((packed));

    struct TSS
    {
        QC::u32 reserved0;
        QC::u64 rsp0;
        QC::u64 rsp1;
        QC::u64 rsp2;
        QC::u64 reserved1;
        QC::u64 ist1;
        QC::u64 ist2;
        QC::u64 ist3;
        QC::u64 ist4;
        QC::u64 ist5;
        QC::u64 ist6;
        QC::u64 ist7;
        QC::u64 reserved2;
        QC::u16 reserved3;
        QC::u16 iopbOffset;
    } __attribute__((packed));

    class GDT
    {
    public:
        static GDT &instance();

        void initialize();
        void load();

        void setKernelStack(QC::VirtAddr stack);

        // Segment selectors
        static constexpr QC::u16 KERNEL_CODE = 0x08;
        static constexpr QC::u16 KERNEL_DATA = 0x10;
        static constexpr QC::u16 USER_CODE = 0x18 | 3;
        static constexpr QC::u16 USER_DATA = 0x20 | 3;
        static constexpr QC::u16 TSS_SELECTOR = 0x28;

    private:
        GDT();
        ~GDT();
        GDT(const GDT &) = delete;
        GDT &operator=(const GDT &) = delete;

        void setEntry(QC::usize index, QC::u32 base, QC::u32 limit,
                      QC::u8 access, QC::u8 granularity);
        void setTSSEntry(QC::usize index, QC::u64 base, QC::u32 limit);

        static constexpr QC::usize GDT_ENTRIES = 7;

        GDTEntry m_entries[GDT_ENTRIES];
        GDTPointer m_pointer;
        TSS m_tss;
    };

} // namespace QArch
