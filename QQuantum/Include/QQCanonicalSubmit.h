#pragma once

#include "QCTypes.h"
#include "QCCanonicalArgs.h"

#include "QQExecutor.h"

namespace QQ
{
    // Convenience wrapper: builds a canonical arg buffer and submits it.
    // Note: caller owns the lifetime of `outBuf` (it must remain valid until the task executes).
    inline TaskId submitCanonical(const char *name,
                                 const char *origin,
                                 const char *moduleId,
                                 TaskFunction func,
                                 void *context,
                                 void *outBuf,
                                 QC::usize outBufCap,
                                 QC::u32 schemaId,
                                 QC::u32 version,
                                 const void *payload,
                                 QC::usize payloadLen)
    {
        QC::usize wrote = 0;
        if (!QC::CanonicalArgs::build(outBuf, outBufCap, schemaId, version, payload, payloadLen, wrote))
            return INVALID_TASK;

        return Executor::instance().submitWithOriginAndArgSize(name, origin, moduleId, func, context, outBuf, wrote);
    }
}
