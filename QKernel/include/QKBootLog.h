#pragma once

#include "QCTypes.h"

namespace QK::Boot::Log
{
    // Clears the captured boot log buffer.
    void Clear();

    // Appends raw text to the captured boot log buffer.
    // The string is treated as a byte stream (may include \r\n).
    void Append(const char *text);

    // Returns the current captured size, in bytes.
    QC::usize Size();

    // Copies up to `cap` bytes starting at `offset` (0 = oldest byte) into `out`.
    // Returns number of bytes copied.
    QC::usize CopyOut(QC::usize offset, char *out, QC::usize cap);
}
