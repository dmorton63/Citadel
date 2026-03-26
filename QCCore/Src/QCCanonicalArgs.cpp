#include "QCCanonicalArgs.h"

#include "QCString.h"

namespace QC
{
    namespace CanonicalArgs
    {
        static bool writeU32Le(QC::u8 *out, QC::usize outCap, QC::usize &ioOff, QC::u32 v)
        {
            if (!out)
                return false;
            if (ioOff + 4 > outCap)
                return false;
            out[ioOff + 0] = static_cast<QC::u8>(v & 0xFF);
            out[ioOff + 1] = static_cast<QC::u8>((v >> 8) & 0xFF);
            out[ioOff + 2] = static_cast<QC::u8>((v >> 16) & 0xFF);
            out[ioOff + 3] = static_cast<QC::u8>((v >> 24) & 0xFF);
            ioOff += 4;
            return true;
        }

        static bool readU32Le(const QC::u8 *buf, QC::usize bufLen, QC::usize &ioOff, QC::u32 &out)
        {
            if (!buf)
                return false;
            if (ioOff + 4 > bufLen)
                return false;
            out = static_cast<QC::u32>(buf[ioOff + 0]) |
                  (static_cast<QC::u32>(buf[ioOff + 1]) << 8) |
                  (static_cast<QC::u32>(buf[ioOff + 2]) << 16) |
                  (static_cast<QC::u32>(buf[ioOff + 3]) << 24);
            ioOff += 4;
            return true;
        }

        bool build(void *out, QC::usize outCap,
                   QC::u32 schemaId, QC::u32 version,
                   const void *payload, QC::usize payloadLen,
                   QC::usize &outSize)
        {
            outSize = 0;
            if (!out)
                return false;

            const QC::usize needed = sizeFor(payloadLen);
            if (outCap < needed)
                return false;

            // Allow empty payload with nullptr.
            if (payloadLen && !payload)
                return false;

            QC::u8 *dst = static_cast<QC::u8 *>(out);
            QC::usize off = 0;

            if (!writeU32Le(dst, outCap, off, schemaId))
                return false;
            if (!writeU32Le(dst, outCap, off, version))
                return false;
            if (!writeU32Le(dst, outCap, off, static_cast<QC::u32>(payloadLen)))
                return false;

            if (payloadLen)
            {
                if (off + payloadLen > outCap)
                    return false;
                QC::String::memcpy(dst + off, payload, payloadLen);
                off += payloadLen;
            }

            outSize = off;
            return outSize == needed;
        }

        bool parse(const void *buf, QC::usize bufLen,
                   Header &outHeader,
                   const void *&outPayload,
                   QC::usize &outPayloadLen)
        {
            outPayload = nullptr;
            outPayloadLen = 0;
            QC::String::memset(&outHeader, 0, sizeof(outHeader));

            if (!buf)
                return false;
            if (bufLen < sizeof(Header))
                return false;

            const QC::u8 *p = static_cast<const QC::u8 *>(buf);
            QC::usize off = 0;

            if (!readU32Le(p, bufLen, off, outHeader.schemaId))
                return false;
            if (!readU32Le(p, bufLen, off, outHeader.version))
                return false;

            QC::u32 payloadLenU32 = 0;
            if (!readU32Le(p, bufLen, off, payloadLenU32))
                return false;

            outHeader.payloadLen = payloadLenU32;
            const QC::usize payloadLen = static_cast<QC::usize>(payloadLenU32);

            if (off + payloadLen != bufLen)
                return false;

            outPayload = (payloadLen ? (p + off) : nullptr);
            outPayloadLen = payloadLen;
            return true;
        }
    }
}
