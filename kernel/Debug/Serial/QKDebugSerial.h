#pragma once

#include "QCTypes.h"

namespace QK::Debug::Serial
{
    using FMirrorCallback = void (*)(const char *);

    void Init();
    void SetMirror(FMirrorCallback MirrorCallback);

    void Write(const char *Message);
    void WriteMirrored(const char *Message);
    void WriteInt(QC::i32 Value);

    const char *CaptureData();
    QC::usize CaptureSize();
    bool CaptureTruncated();
}
