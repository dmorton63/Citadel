#pragma once

#include "QCTypes.h"

namespace QK::Boot::Events
{
    using LogFn = void (*)(const char *);

    struct Record
    {
        QC::u32 seq = 0;
        QC::u64 t_ms = 0;

        char stage[16] = {0};
        char type[32] = {0};
        char details[192] = {0};
    };

    void Clear();

    // Optional: when set, each emitted event is also written as a compact one-line string.
    void SetSerialSink(LogFn sink);

    // Emits a new boot event.
    // - stage: "early|bootpolicy|sysconfig|services|desktop" (recommended)
    // - type:  short token (e.g. "config_select", "config_validate_fail")
    // - details: compact key/value string (e.g. "tier=production reason='JSON parse failed'")
    QC::u32 Emit(const char *stage, const char *type, const char *details);

    QC::usize Count();

    // Copies up to `cap` events starting at `offset` (0 = oldest event) into `out`.
    // Returns number of events copied.
    QC::usize CopyOut(QC::usize offset, Record *out, QC::usize cap);
}
