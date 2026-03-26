#pragma once

#include "QCTypes.h"

namespace QC
{
    namespace CanonicalArgs
    {
        // Canonical argument buffer layout (little-endian):
        //   u32 schemaId
        //   u32 version
        //   u32 payloadLen
        //   u8  payload[payloadLen]
        // Intended use: stable hashing + cache keying.

        struct Header
        {
            QC::u32 schemaId;
            QC::u32 version;
            QC::u32 payloadLen;
        };

        // Returns required size for a canonical buffer.
        constexpr QC::usize sizeFor(QC::usize payloadLen)
        {
            return sizeof(Header) + payloadLen;
        }

        // Builds a canonical arg buffer into `out`.
        // Returns false on invalid args or insufficient capacity.
        bool build(void *out, QC::usize outCap,
                   QC::u32 schemaId, QC::u32 version,
                   const void *payload, QC::usize payloadLen,
                   QC::usize &outSize);

        // Best-effort validation + parse.
        // Returns false if the buffer is malformed.
        bool parse(const void *buf, QC::usize bufLen,
                   Header &outHeader,
                   const void *&outPayload,
                   QC::usize &outPayloadLen);
    }
}
