// QNetwork DHCP - Dynamic Host Configuration Protocol (IPv4) implementation
// Namespace: QNet

#include "QNetDHCP.h"

#include "QNetStack.h"
#include "QNetEthernet.h"
#include "QNetUDP.h"

#include "QCMemUtil.h"

namespace QNet
{

    // Byte swap utilities
    static inline QC::u16 htons(QC::u16 val)
    {
        return (val >> 8) | (val << 8);
    }

    static inline QC::u16 ntohs(QC::u16 val)
    {
        return htons(val);
    }

    static inline QC::u32 htonl(QC::u32 val)
    {
        return ((val >> 24) & 0x000000FF) |
               ((val >> 8) & 0x0000FF00) |
               ((val << 8) & 0x00FF0000) |
               ((val << 24) & 0xFF000000);
    }

    static inline QC::u32 ntohl(QC::u32 val)
    {
        return htonl(val);
    }

    namespace
    {
        constexpr QC::u8 BOOTREQUEST = 1;
        constexpr QC::u8 BOOTREPLY = 2;

        constexpr QC::u8 HTYPE_ETHERNET = 1;
        constexpr QC::u8 HLEN_ETHERNET = 6;

        constexpr QC::u16 DHCP_CLIENT_PORT = 68;
        constexpr QC::u16 DHCP_SERVER_PORT = 67;

        // DHCP options
        constexpr QC::u8 OPT_PAD = 0;
        constexpr QC::u8 OPT_SUBNET_MASK = 1;
        constexpr QC::u8 OPT_ROUTER = 3;
        constexpr QC::u8 OPT_DNS = 6;
        constexpr QC::u8 OPT_REQ_IP = 50;
        constexpr QC::u8 OPT_LEASE_TIME = 51;
        constexpr QC::u8 OPT_MSG_TYPE = 53;
        constexpr QC::u8 OPT_SERVER_ID = 54;
        constexpr QC::u8 OPT_PARAM_REQ_LIST = 55;
        constexpr QC::u8 OPT_MAX_MSG_SIZE = 57;
        constexpr QC::u8 OPT_CLIENT_ID = 61;
        constexpr QC::u8 OPT_END = 255;

        // DHCP message types
        constexpr QC::u8 DHCPDISCOVER = 1;
        constexpr QC::u8 DHCPOFFER = 2;
        constexpr QC::u8 DHCPREQUEST = 3;
        constexpr QC::u8 DHCPACK = 5;
        constexpr QC::u8 DHCPNAK = 6;

        struct BOOTPHeader
        {
            QC::u8 op;
            QC::u8 htype;
            QC::u8 hlen;
            QC::u8 hops;
            QC::u32 xid;
            QC::u16 secs;
            QC::u16 flags;
            QC::u32 ciaddr;
            QC::u32 yiaddr;
            QC::u32 siaddr;
            QC::u32 giaddr;
            QC::u8 chaddr[16];
            QC::u8 sname[64];
            QC::u8 file[128];
        } __attribute__((packed));

        static void WriteMagicCookie(QC::u8 *p)
        {
            p[0] = 0x63;
            p[1] = 0x82;
            p[2] = 0x53;
            p[3] = 0x63;
        }

        static bool HasMagicCookie(const QC::u8 *p)
        {
            return p[0] == 0x63 && p[1] == 0x82 && p[2] == 0x53 && p[3] == 0x63;
        }

        static bool ReadOptionU32(const QC::u8 *data, QC::usize length, QC::u32 *outHostU32)
        {
            if (!outHostU32 || length < 4)
                return false;
            QC::u32 be = 0;
            be |= (static_cast<QC::u32>(data[0]) << 24);
            be |= (static_cast<QC::u32>(data[1]) << 16);
            be |= (static_cast<QC::u32>(data[2]) << 8);
            be |= (static_cast<QC::u32>(data[3]) << 0);
            *outHostU32 = ntohl(be);
            return true;
        }

        static bool ReadOptionIPv4(const QC::u8 *data, QC::usize length, IPv4Address *out)
        {
            if (!out || length < 4)
                return false;
            out->octets[0] = data[0];
            out->octets[1] = data[1];
            out->octets[2] = data[2];
            out->octets[3] = data[3];
            return true;
        }

        static void PutOption(QC::u8 *&opt, QC::u8 code, const void *value, QC::u8 len)
        {
            *opt++ = code;
            *opt++ = len;
            const auto *v = static_cast<const QC::u8 *>(value);
            for (QC::u8 i = 0; i < len; ++i)
                *opt++ = v[i];
        }

        static void PutOptionU8(QC::u8 *&opt, QC::u8 code, QC::u8 value)
        {
            PutOption(opt, code, &value, 1);
        }

        static void PutOptionU16BE(QC::u8 *&opt, QC::u8 code, QC::u16 value)
        {
            QC::u8 bytes[2] = {
                static_cast<QC::u8>((value >> 8) & 0xFF),
                static_cast<QC::u8>((value >> 0) & 0xFF),
            };
            PutOption(opt, code, bytes, 2);
        }

        static void PutOptionIPv4(QC::u8 *&opt, QC::u8 code, IPv4Address addr)
        {
            PutOption(opt, code, addr.octets, 4);
        }

        static void PutParamRequestList(QC::u8 *&opt)
        {
            QC::u8 params[3] = {OPT_SUBNET_MASK, OPT_ROUTER, OPT_DNS};
            PutOption(opt, OPT_PARAM_REQ_LIST, params, sizeof(params));
        }

    } // namespace

    DHCPv4Client::DHCPv4Client()
        : m_state(State::Idle), m_binding(nullptr), m_xid(0xC17ADE01u), m_leaseTimeSec(0)
    {
        m_offeredAddr.value = 0;
        m_subnetMask.value = 0;
        m_gateway.value = 0;
        m_dnsServer.value = 0;
        m_serverId.value = 0;
    }

    DHCPv4Client::~DHCPv4Client()
    {
        reset();
    }

    void DHCPv4Client::reset()
    {
        if (m_binding)
        {
            Stack::instance().udp()->unbind(m_binding);
            m_binding = nullptr;
        }

        m_state = State::Idle;
        m_offeredAddr.value = 0;
        m_subnetMask.value = 0;
        m_gateway.value = 0;
        m_dnsServer.value = 0;
        m_serverId.value = 0;
        m_leaseTimeSec = 0;
    }

    bool DHCPv4Client::macLooksValid() const
    {
        auto *eth = Stack::instance().ethernet();
        if (!eth)
            return false;

        const MACAddress mac = eth->macAddress();
        bool allZero = true;
        for (int i = 0; i < 6; ++i)
        {
            if (mac.bytes[i] != 0)
            {
                allZero = false;
                break;
            }
        }
        return !allZero;
    }

    bool DHCPv4Client::macMatches(const QC::u8 *chaddr16, const QC::u8 mac6[6])
    {
        if (!chaddr16)
            return false;
        for (int i = 0; i < 6; ++i)
        {
            if (chaddr16[i] != mac6[i])
                return false;
        }
        return true;
    }

    QC::Status DHCPv4Client::begin()
    {
        reset();

        if (!macLooksValid())
        {
            m_state = State::Failed;
            return QC::Status::Error;
        }

        m_binding = Stack::instance().udp()->bind(DHCP_CLIENT_PORT);
        if (!m_binding)
        {
            m_state = State::Failed;
            return QC::Status::Busy;
        }

        // Bump transaction id each run.
        m_xid += 0x01010101u;

        QC::Status st = sendDiscover();
        if (st != QC::Status::Success)
        {
            m_state = State::Failed;
            return st;
        }

        m_state = State::DiscoverSent;
        return QC::Status::Success;
    }

    bool DHCPv4Client::poll(DHCPv4Lease *outLease)
    {
        if (!outLease)
            return false;

        if (m_state == State::Idle || m_state == State::Failed || m_state == State::Bound)
            return (m_state == State::Bound);

        // Drain receive queue; handle at most a few packets per poll.
        for (int i = 0; i < 8; ++i)
        {
            QC::u8 buf[600];
            IPv4Address src{};
            QC::u16 srcPort = 0;

            const QC::isize n = Stack::instance().udp()->receive(m_binding, buf, sizeof(buf), &src, &srcPort);
            if (n <= 0)
                break;

            if (handleDhcpPacket(buf, static_cast<QC::usize>(n), outLease))
            {
                return true;
            }
        }

        return false;
    }

    bool DHCPv4Client::handleDhcpPacket(const void *data, QC::usize length, DHCPv4Lease *outLease)
    {
        if (!data || length < sizeof(BOOTPHeader) + 4)
            return false;

        const auto *bootp = static_cast<const BOOTPHeader *>(data);
        if (bootp->op != BOOTREPLY || bootp->htype != HTYPE_ETHERNET || bootp->hlen != HLEN_ETHERNET)
            return false;

        const QC::u32 xid = ntohl(bootp->xid);
        if (xid != m_xid)
            return false;

        auto *eth = Stack::instance().ethernet();
        if (!eth)
            return false;
        const MACAddress mac = eth->macAddress();
        if (!macMatches(bootp->chaddr, mac.bytes))
            return false;

        const auto *cookie = reinterpret_cast<const QC::u8 *>(data) + sizeof(BOOTPHeader);
        if (!HasMagicCookie(cookie))
            return false;

        const QC::u8 *options = cookie + 4;
        const QC::usize optLen = length - (sizeof(BOOTPHeader) + 4);

        QC::u8 msgType = 0;
        // parseOptions fills member fields as it sees options.
        if (!parseOptions(options, optLen, &msgType))
            return false;

        // yiaddr is our offered/assigned address. Keep raw bytes as-is.
        IPv4Address yiaddr{};
        yiaddr.value = bootp->yiaddr;
        if (yiaddr.value == 0)
            return false;

        if (msgType == DHCPOFFER)
        {
            m_offeredAddr = yiaddr;

            if (m_serverId.value != 0)
            {
                (void)sendRequest();
                m_state = State::RequestSent;
            }

            return false;
        }

        if (msgType == DHCPACK)
        {
            outLease->address = yiaddr;
            if (m_subnetMask.value != 0)
            {
                outLease->subnetMask = m_subnetMask;
            }
            else
            {
                outLease->subnetMask.octets[0] = 255;
                outLease->subnetMask.octets[1] = 255;
                outLease->subnetMask.octets[2] = 255;
                outLease->subnetMask.octets[3] = 0;
            }
            outLease->gateway = m_gateway;
            // Some DHCP servers (or minimal implementations) may omit option 3 (Router).
            // In our typical QEMU SLIRP environment, the DHCP server is also the gateway.
            if (outLease->gateway.value == 0 && m_serverId.value != 0)
            {
                outLease->gateway = m_serverId;
            }
            outLease->dnsServer = m_dnsServer;
            outLease->serverId = m_serverId;
            outLease->leaseTimeSec = m_leaseTimeSec;

            m_state = State::Bound;
            return true;
        }

        if (msgType == DHCPNAK)
        {
            m_state = State::Failed;
            return false;
        }

        return false;
    }

    bool DHCPv4Client::parseOptions(const QC::u8 *options, QC::usize length, QC::u8 *outMsgType)
    {
        if (!options || !outMsgType)
            return false;

        *outMsgType = 0;

        QC::usize i = 0;
        while (i < length)
        {
            const QC::u8 code = options[i++];
            if (code == OPT_PAD)
                continue;
            if (code == OPT_END)
                break;
            if (i >= length)
                break;

            const QC::u8 optLen = options[i++];
            if (i + optLen > length)
                break;

            const QC::u8 *data = &options[i];

            switch (code)
            {
            case OPT_MSG_TYPE:
                if (optLen >= 1)
                    *outMsgType = data[0];
                break;

            case OPT_SERVER_ID:
                if (optLen == 4)
                {
                    (void)ReadOptionIPv4(data, optLen, &m_serverId);
                }
                break;

            case OPT_SUBNET_MASK:
                if (optLen == 4)
                {
                    (void)ReadOptionIPv4(data, optLen, &m_subnetMask);
                }
                break;

            case OPT_ROUTER:
                if (optLen >= 4)
                {
                    (void)ReadOptionIPv4(data, 4, &m_gateway);
                }
                break;

            case OPT_DNS:
                if (optLen >= 4)
                {
                    (void)ReadOptionIPv4(data, 4, &m_dnsServer);
                }
                break;

            case OPT_LEASE_TIME:
                if (optLen == 4)
                {
                    QC::u32 host = 0;
                    if (ReadOptionU32(data, optLen, &host))
                        m_leaseTimeSec = host;
                }
                break;

            default:
                break;
            }

            i += optLen;
        }

        return (*outMsgType != 0);
    }

    QC::Status DHCPv4Client::sendDiscover()
    {
        IPv4Address broadcast{};
        broadcast.value = 0xFFFFFFFFu;

        QC::u8 packet[300];
        memset(packet, 0, sizeof(packet));

        auto *bootp = reinterpret_cast<BOOTPHeader *>(packet);
        bootp->op = BOOTREQUEST;
        bootp->htype = HTYPE_ETHERNET;
        bootp->hlen = HLEN_ETHERNET;
        bootp->hops = 0;
        bootp->xid = htonl(m_xid);
        bootp->secs = htons(0);
        bootp->flags = htons(0x8000);

        const MACAddress mac = Stack::instance().ethernet()->macAddress();
        for (int i = 0; i < 6; ++i)
            bootp->chaddr[i] = mac.bytes[i];

        QC::u8 *cookie = packet + sizeof(BOOTPHeader);
        WriteMagicCookie(cookie);

        QC::u8 *opt = cookie + 4;
        PutOptionU8(opt, OPT_MSG_TYPE, DHCPDISCOVER);

        // Client identifier: htype(1=Ethernet) + MAC.
        {
            QC::u8 cid[7];
            cid[0] = HTYPE_ETHERNET;
            for (int i = 0; i < 6; ++i)
                cid[1 + i] = mac.bytes[i];
            PutOption(opt, OPT_CLIENT_ID, cid, sizeof(cid));
        }

        // Let servers know how big a DHCP message we can receive.
        PutOptionU16BE(opt, OPT_MAX_MSG_SIZE, 576);

        PutParamRequestList(opt);
        *opt++ = OPT_END;

        QC::usize used = static_cast<QC::usize>(opt - packet);
        // RFC 2131: clients should send at least 300 bytes.
        if (used < 300)
            used = 300;
        if (used > sizeof(packet))
            used = sizeof(packet);

        return Stack::instance().udp()->send(broadcast, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, packet, used);
    }

    QC::Status DHCPv4Client::sendRequest()
    {
        if (m_offeredAddr.value == 0 || m_serverId.value == 0)
            return QC::Status::Error;

        IPv4Address broadcast{};
        broadcast.value = 0xFFFFFFFFu;

        QC::u8 packet[320];
        memset(packet, 0, sizeof(packet));

        auto *bootp = reinterpret_cast<BOOTPHeader *>(packet);
        bootp->op = BOOTREQUEST;
        bootp->htype = HTYPE_ETHERNET;
        bootp->hlen = HLEN_ETHERNET;
        bootp->hops = 0;
        bootp->xid = htonl(m_xid);
        bootp->secs = htons(0);
        bootp->flags = htons(0x8000);

        const MACAddress mac = Stack::instance().ethernet()->macAddress();
        for (int i = 0; i < 6; ++i)
            bootp->chaddr[i] = mac.bytes[i];

        QC::u8 *cookie = packet + sizeof(BOOTPHeader);
        WriteMagicCookie(cookie);

        QC::u8 *opt = cookie + 4;
        PutOptionU8(opt, OPT_MSG_TYPE, DHCPREQUEST);

        // Client identifier: htype(1=Ethernet) + MAC.
        {
            QC::u8 cid[7];
            cid[0] = HTYPE_ETHERNET;
            for (int i = 0; i < 6; ++i)
                cid[1 + i] = mac.bytes[i];
            PutOption(opt, OPT_CLIENT_ID, cid, sizeof(cid));
        }

        PutOptionU16BE(opt, OPT_MAX_MSG_SIZE, 576);

        PutOptionIPv4(opt, OPT_REQ_IP, m_offeredAddr);
        PutOptionIPv4(opt, OPT_SERVER_ID, m_serverId);
        PutParamRequestList(opt);
        *opt++ = OPT_END;

        QC::usize used = static_cast<QC::usize>(opt - packet);
        if (used < 300)
            used = 300;
        if (used > sizeof(packet))
            used = sizeof(packet);

        return Stack::instance().udp()->send(broadcast, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, packet, used);
    }

} // namespace QNet
