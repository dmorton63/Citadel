#pragma once

#include "QKBootTPMInternal.h"

namespace QK::Boot::Tpm::Diag
{
    // Caches vendor, firmware version, and self-test state. Called once after
    // TPM startup with the TPM operation lock held.
    void CaptureStartupInfo(FLogFn Log);
}
