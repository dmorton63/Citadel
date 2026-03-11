// QNetwork DNS - minimal DNS client implementation
// Namespace: QNet

#include "QNetDNS.h"

#include "QNetStack.h"
#include "QNetUDP.h"

#include "QCString.h"

namespace QNet
{

    static inline QC::u16 htons(QC::u16 v)
    {
        return static_cast<QC::u16>((v >> 8) | (v << 8));
    }

    static inline QC::u16 ntohs(QC::u16 v)
    {
        return htons(v);
    }

    static inline QC::u32 ntohl(QC::u32 v)
    {
        return ((v >> 24) & 0x000000FF) |
               ((v >> 8) & 0x0000FF00) |
               ((v << 8) & 0x00FF0000) |
               ((v << 24) & 0xFF000000);
    }

    struct DNSHeader
    {
        QC::u16 id;
        QC::u16 flags;
        QC::u16 qdcount;
        QC::u16 ancount;
        QC::u16 nscount;
        QC::u16 arcount;
    } __attribute__((packed));

    DNSClient::DNSClient()
        : m_binding(nullptr), m_server{}, m_txid(0), m_active(false)
    {
    }

    DNSClient::~DNSClient()
    {
        reset();
    }

    void DNSClient::reset()
    {
        if (m_binding)
        {
            Stack::instance().udp()->unbind(m_binding);
            m_binding = nullptr;
        }
        m_active = false;
        m_server.value = 0;
        m_txid = 0;
    }

    QC::Status DNSClient::begin(IPv4Address server, const char *name, QC::u16 txid)
    {
        reset();

        if (!name || *name == '\0')
            return QC::Status::InvalidParam;
        if (server.value == 0)
            return QC::Status::InvalidParam;

        m_server = server;
        m_txid = txid;

        m_binding = Stack::instance().udp()->bind(0);
        if (!m_binding)
            return QC::Status::OutOfMemory;

        const QC::Status st = sendQuery(name);
        if (st != QC::Status::Success)
        {
            reset();
            return st;
        }

        m_active = true;
        return QC::Status::Success;
    }

    QC::Status DNSClient::sendQuery(const char *name)
    {
        // RFC 1035 UDP payload is typically 512 bytes without EDNS.
        QC::u8 buf[512];
        QC::String::memset(buf, 0, sizeof(buf));

        if (sizeof(DNSHeader) + 5 > sizeof(buf))
            return QC::Status::OutOfMemory;

        auto *hdr = reinterpret_cast<DNSHeader *>(buf);
        hdr->id = htons(m_txid);
        // flags: QR=0, RD=1
        hdr->flags = htons(0x0100);
        hdr->qdcount = htons(1);
        hdr->ancount = 0;
        hdr->nscount = 0;
        hdr->arcount = 0;

        QC::usize off = sizeof(DNSHeader);

        // Encode QNAME.
        const char *p = name;
        while (*p)
        {
            // label length
            const char *dot = p;
            while (*dot && *dot != '.')
                ++dot;

            const QC::usize labelLen = static_cast<QC::usize>(dot - p);
            if (labelLen == 0 || labelLen > 63)
                return QC::Status::InvalidParam;
            if (off + 1 + labelLen >= sizeof(buf))
                return QC::Status::OutOfMemory;

            buf[off++] = static_cast<QC::u8>(labelLen);
            for (QC::usize i = 0; i < labelLen; ++i)
                buf[off++] = static_cast<QC::u8>(p[i]);

            p = dot;
            if (*p == '.')
                ++p;
        }

        if (off + 1 + 4 > sizeof(buf))
            return QC::Status::OutOfMemory;

        buf[off++] = 0; // end name

        // QTYPE=A (1), QCLASS=IN (1)
        buf[off++] = 0;
        buf[off++] = 1;
        buf[off++] = 0;
        buf[off++] = 1;

        return Stack::instance().udp()->send(m_server, 53, m_binding->port, buf, off);
    }

    bool DNSClient::skipName(const QC::u8 *msg, QC::usize msgLen, QC::usize &offset)
    {
        // Follows RFC 1035 name encoding enough to advance the cursor. Allows compression pointers.
        if (!msg)
            return false;

        QC::usize steps = 0;
        while (offset < msgLen && steps++ < 64)
        {
            const QC::u8 len = msg[offset];
            if (len == 0)
            {
                offset += 1;
                return true;
            }

            // compression pointer: 11xxxxxx xxxxxxxx
            if ((len & 0xC0) == 0xC0)
            {
                if (offset + 2 > msgLen)
                    return false;
                offset += 2;
                return true;
            }

            if (len & 0xC0)
                return false;

            if (offset + 1 + len > msgLen)
                return false;
            offset += 1 + len;
        }

        return false;
    }

    bool DNSClient::poll(IPv4Address *outAddress)
    {
        if (!m_active || !m_binding)
            return false;

        QC::u8 buf[512];
        IPv4Address src{};
        QC::u16 srcPort = 0;
        const QC::isize n = Stack::instance().udp()->receive(m_binding, buf, sizeof(buf), &src, &srcPort);
        if (n <= 0)
            return false;

        const QC::usize len = static_cast<QC::usize>(n);
        if (len < sizeof(DNSHeader))
            return false;

        const auto *hdr = reinterpret_cast<const DNSHeader *>(buf);
        if (ntohs(hdr->id) != m_txid)
            return false;

        // Must be a response.
        const QC::u16 flags = ntohs(hdr->flags);
        const bool qr = (flags & 0x8000) != 0;
        const QC::u16 rcode = (flags & 0x000F);
        if (!qr || rcode != 0)
            return false;

        const QC::u16 qd = ntohs(hdr->qdcount);
        const QC::u16 an = ntohs(hdr->ancount);

        QC::usize off = sizeof(DNSHeader);

        // Skip questions.
        for (QC::u16 i = 0; i < qd; ++i)
        {
            if (!skipName(buf, len, off))
                return false;
            if (off + 4 > len)
                return false;
            off += 4; // type+class
        }

        // Parse answers; return first A record.
        for (QC::u16 i = 0; i < an; ++i)
        {
            if (!skipName(buf, len, off))
                return false;
            if (off + 10 > len)
                return false;

            const QC::u16 type = static_cast<QC::u16>((buf[off] << 8) | buf[off + 1]);
            const QC::u16 cls = static_cast<QC::u16>((buf[off + 2] << 8) | buf[off + 3]);
            (void)ntohl(*reinterpret_cast<const QC::u32 *>(buf + off + 4)); // ttl
            const QC::u16 rdlen = static_cast<QC::u16>((buf[off + 8] << 8) | buf[off + 9]);
            off += 10;

            if (off + rdlen > len)
                return false;

            if (type == 1 && cls == 1 && rdlen == 4)
            {
                if (outAddress)
                {
                    IPv4Address a{};
                    a.octets[0] = buf[off + 0];
                    a.octets[1] = buf[off + 1];
                    a.octets[2] = buf[off + 2];
                    a.octets[3] = buf[off + 3];
                    *outAddress = a;
                }

                m_active = false;
                return true;
            }

            off += rdlen;
        }

        return false;
    }

} // namespace QNet
