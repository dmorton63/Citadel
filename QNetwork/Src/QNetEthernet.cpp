// QNetwork Ethernet - Ethernet layer implementation
// Namespace: QNet

#include "QNetEthernet.h"
#include "QNetStack.h"
#include "QNetIP.h"
#include "QCMemUtil.h"
#include "QKMemHeap.h"

namespace QNet
{

    // MACAddress implementation
    bool MACAddress::operator==(const MACAddress &other) const
    {
        for (int i = 0; i < 6; i++)
        {
            if (bytes[i] != other.bytes[i])
                return false;
        }
        return true;
    }

    bool MACAddress::isBroadcast() const
    {
        for (int i = 0; i < 6; i++)
        {
            if (bytes[i] != 0xFF)
                return false;
        }
        return true;
    }

    bool MACAddress::isMulticast() const
    {
        return (bytes[0] & 0x01) != 0;
    }

    // ARP packet structure
    struct ARPPacket
    {
        QC::u16 hardwareType;
        QC::u16 protocolType;
        QC::u8 hardwareAddrLen;
        QC::u8 protocolAddrLen;
        QC::u16 operation;
        MACAddress senderMAC;
        QC::u32 senderIP;
        MACAddress targetMAC;
        QC::u32 targetIP;
    } __attribute__((packed));

    namespace ARPOperation
    {
        constexpr QC::u16 Request = 1;
        constexpr QC::u16 Reply = 2;
    }

    // Byte swap utilities for network byte order
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

    Ethernet::Ethernet()
    {
        memset(&m_mac, 0, sizeof(m_mac));
        memset(m_arpCache, 0, sizeof(m_arpCache));
        memset(m_pending, 0, sizeof(m_pending));
        memset(m_arpInFlight, 0, sizeof(m_arpInFlight));
        m_nowMs = 0;
    }

    Ethernet::~Ethernet()
    {
        for (QC::usize i = 0; i < PENDING_MAX; ++i)
        {
            if (m_pending[i].valid)
            {
                if (m_pending[i].payload)
                    QK::Memory::Heap::instance().free(m_pending[i].payload);
                m_pending[i].payload = nullptr;
                m_pending[i].length = 0;
                m_pending[i].valid = false;
            }
        }
    }

    void Ethernet::initialize()
    {
        // Clear ARP cache
        for (QC::usize i = 0; i < ARP_CACHE_SIZE; i++)
        {
            m_arpCache[i].valid = false;
        }

        for (QC::usize i = 0; i < ARP_INFLIGHT_MAX; ++i)
        {
            m_arpInFlight[i].valid = false;
            m_arpInFlight[i].ip = 0;
            m_arpInFlight[i].lastRequestMs = 0;
            m_arpInFlight[i].retries = 0;
        }

        for (QC::usize i = 0; i < PENDING_MAX; ++i)
        {
            m_pending[i].valid = false;
            m_pending[i].payload = nullptr;
            m_pending[i].length = 0;
            m_pending[i].nextHopIp = 0;
            m_pending[i].etherType = 0;
        }
    }

    void Ethernet::poll(QC::u64 nowMs)
    {
        m_nowMs = nowMs;

        // Age out ARP cache entries (best-effort; only if timestamps are populated).
        constexpr QC::u64 kArpTtlMs = 5ull * 60ull * 1000ull;
        if (nowMs != 0)
        {
            for (QC::usize i = 0; i < ARP_CACHE_SIZE; ++i)
            {
                if (!m_arpCache[i].valid)
                    continue;
                if (m_arpCache[i].timestamp == 0)
                    continue;
                if ((nowMs - m_arpCache[i].timestamp) > kArpTtlMs)
                {
                    m_arpCache[i].valid = false;
                }
            }
        }

        // Retry ARP requests for any next-hop with queued packets.
        constexpr QC::u64 kRetryIntervalMs = 500;
        constexpr QC::u8 kMaxRetries = 4;

        for (QC::usize pi = 0; pi < PENDING_MAX; ++pi)
        {
            if (!m_pending[pi].valid)
                continue;
            const QC::u32 ip = m_pending[pi].nextHopIp;
            if (ip == 0)
                continue;

            // If already resolved, flush will happen via ARP update path.
            bool resolved = false;
            for (QC::usize i = 0; i < ARP_CACHE_SIZE; ++i)
            {
                if (m_arpCache[i].valid && m_arpCache[i].ip == ip)
                {
                    resolved = true;
                    break;
                }
            }
            if (resolved)
                continue;

            // Determine inflight slot.
            QC::usize slot = ARP_INFLIGHT_MAX;
            for (QC::usize i = 0; i < ARP_INFLIGHT_MAX; ++i)
            {
                if (m_arpInFlight[i].valid && m_arpInFlight[i].ip == ip)
                {
                    slot = i;
                    break;
                }
            }

            if (slot == ARP_INFLIGHT_MAX)
            {
                // Create inflight record and send request now.
                noteArpRequest(ip);
                continue;
            }

            if (nowMs != 0 && m_arpInFlight[slot].lastRequestMs != 0)
            {
                if ((nowMs - m_arpInFlight[slot].lastRequestMs) < kRetryIntervalMs)
                    continue;
            }

            if (m_arpInFlight[slot].retries >= kMaxRetries)
            {
                dropPendingFor(ip);
                clearArpInFlight(ip);
                continue;
            }

            sendARPRequest(ip);
            m_arpInFlight[slot].lastRequestMs = nowMs;
            m_arpInFlight[slot].retries++;
        }
    }

    void Ethernet::receiveFrame(const void *data, QC::usize length)
    {
        if (length < sizeof(EthernetHeader))
            return;

        const auto *header = static_cast<const EthernetHeader *>(data);

        // Check if frame is for us (unicast, broadcast, or multicast)
        if (!header->destination.isBroadcast() &&
            !header->destination.isMulticast() &&
            !(header->destination == m_mac))
        {
            return; // Not for us
        }

        QC::u16 etherType = ntohs(header->etherType);
        const void *payload = static_cast<const QC::u8 *>(data) + sizeof(EthernetHeader);
        QC::usize payloadLen = length - sizeof(EthernetHeader);

        switch (etherType)
        {
        case EtherType::IPv4:
            Stack::instance().ip()->receivePacket(payload, payloadLen);
            break;

        case EtherType::ARP:
            handleARP(payload, payloadLen);
            break;

        case EtherType::IPv6:
            // IPv6 not yet supported
            break;
        }
    }

    void Ethernet::sendFrame(const MACAddress &dest, QC::u16 etherType,
                             const void *payload, QC::usize length)
    {
        // Allocate buffer for full frame
        QC::usize frameSize = sizeof(EthernetHeader) + length;
        QC::u8 *frame = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(frameSize));
        if (!frame)
            return;

        // Build header
        auto *header = reinterpret_cast<EthernetHeader *>(frame);
        header->destination = dest;
        header->source = m_mac;
        header->etherType = htons(etherType);

        // Copy payload
        memcpy(frame + sizeof(EthernetHeader), payload, length);

        // Transmit
        Stack::instance().transmitPacket(frame, frameSize);

        QK::Memory::Heap::instance().free(frame);
    }

    bool Ethernet::resolveMAC(QC::u32 ipAddress, MACAddress *mac)
    {
        // Limited broadcast: map directly to broadcast MAC.
        if (ipAddress == 0xFFFFFFFF)
        {
            memset(mac, 0xFF, sizeof(MACAddress));
            return true;
        }

        // Check ARP cache first
        for (QC::usize i = 0; i < ARP_CACHE_SIZE; i++)
        {
            if (m_arpCache[i].valid && m_arpCache[i].ip == ipAddress)
            {
                *mac = m_arpCache[i].mac;
                return true;
            }
        }

        // Send ARP request (throttled via inflight tracking)
        noteArpRequest(ipAddress);

        // In a real implementation, we'd wait for reply
        // For now, return false to indicate resolution in progress
        return false;
    }

    void Ethernet::updateARPCache(QC::u32 ipAddress, const MACAddress &mac)
    {
        // Look for existing entry
        for (QC::usize i = 0; i < ARP_CACHE_SIZE; i++)
        {
            if (m_arpCache[i].valid && m_arpCache[i].ip == ipAddress)
            {
                m_arpCache[i].mac = mac;
                m_arpCache[i].timestamp = m_nowMs;
                clearArpInFlight(ipAddress);
                return;
            }
        }

        // Find free slot or oldest entry
        QC::usize slot = 0;
        for (QC::usize i = 0; i < ARP_CACHE_SIZE; i++)
        {
            if (!m_arpCache[i].valid)
            {
                slot = i;
                break;
            }
        }

        // Add new entry
        m_arpCache[slot].ip = ipAddress;
        m_arpCache[slot].mac = mac;
        m_arpCache[slot].timestamp = m_nowMs;
        m_arpCache[slot].valid = true;

        clearArpInFlight(ipAddress);
    }

    QC::usize Ethernet::copyARPCache(ARPCacheEntryView *out, QC::usize max) const
    {
        if (!out || max == 0)
            return 0;

        QC::usize n = 0;
        for (QC::usize i = 0; i < ARP_CACHE_SIZE && n < max; ++i)
        {
            if (!m_arpCache[i].valid)
                continue;
            out[n].ip = m_arpCache[i].ip;
            out[n].mac = m_arpCache[i].mac;
            out[n].timestamp = m_arpCache[i].timestamp;
            ++n;
        }
        return n;
    }

    void Ethernet::queuePendingPacket(QC::u32 nextHopIp, QC::u16 etherType,
                                      const void *payload, QC::usize length)
    {
        if (!payload || length == 0)
            return;

        // If already queued for this next hop, replace (keep things minimal).
        QC::usize slot = PENDING_MAX;
        for (QC::usize i = 0; i < PENDING_MAX; ++i)
        {
            if (m_pending[i].valid && m_pending[i].nextHopIp == nextHopIp && m_pending[i].etherType == etherType)
            {
                slot = i;
                break;
            }
        }

        // Otherwise pick an empty slot.
        if (slot == PENDING_MAX)
        {
            for (QC::usize i = 0; i < PENDING_MAX; ++i)
            {
                if (!m_pending[i].valid)
                {
                    slot = i;
                    break;
                }
            }
        }

        if (slot == PENDING_MAX)
        {
            // Queue full; drop.
            return;
        }

        // Replace existing payload if present.
        if (m_pending[slot].valid && m_pending[slot].payload)
        {
            QK::Memory::Heap::instance().free(m_pending[slot].payload);
            m_pending[slot].payload = nullptr;
            m_pending[slot].length = 0;
        }

        QC::u8 *copy = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(length));
        if (!copy)
            return;
        memcpy(copy, payload, length);

        m_pending[slot].valid = true;
        m_pending[slot].nextHopIp = nextHopIp;
        m_pending[slot].etherType = etherType;
        m_pending[slot].payload = copy;
        m_pending[slot].length = length;
    }

    void Ethernet::flushPendingFor(QC::u32 nextHopIp, const MACAddress &mac)
    {
        for (QC::usize i = 0; i < PENDING_MAX; ++i)
        {
            if (!m_pending[i].valid)
                continue;
            if (m_pending[i].nextHopIp != nextHopIp)
                continue;

            sendFrame(mac, m_pending[i].etherType, m_pending[i].payload, m_pending[i].length);

            if (m_pending[i].payload)
                QK::Memory::Heap::instance().free(m_pending[i].payload);
            m_pending[i].payload = nullptr;
            m_pending[i].length = 0;
            m_pending[i].valid = false;
        }
    }

    void Ethernet::handleARP(const void *data, QC::usize length)
    {
        if (length < sizeof(ARPPacket))
            return;

        const auto *arp = static_cast<const ARPPacket *>(data);

        // Only handle Ethernet/IPv4 ARP
        if (ntohs(arp->hardwareType) != 1 || ntohs(arp->protocolType) != EtherType::IPv4)
            return;

        // Update cache with sender info
        updateARPCache(arp->senderIP, arp->senderMAC);
        flushPendingFor(arp->senderIP, arp->senderMAC);

        QC::u16 op = ntohs(arp->operation);
        if (op == ARPOperation::Request)
        {
            // Check if they're asking for our IP
            IPv4Address ourIP = Stack::instance().ip()->address();
            if (arp->targetIP == ourIP.value)
            {
                sendARPReply(arp->senderIP, arp->senderMAC);
            }
        }
    }

    void Ethernet::noteArpRequest(QC::u32 targetIP)
    {
        if (targetIP == 0)
            return;

        // Find existing inflight entry.
        for (QC::usize i = 0; i < ARP_INFLIGHT_MAX; ++i)
        {
            if (m_arpInFlight[i].valid && m_arpInFlight[i].ip == targetIP)
            {
                // Throttle: only re-send if enough time has passed.
                if (m_nowMs == 0 || m_arpInFlight[i].lastRequestMs == 0 || (m_nowMs - m_arpInFlight[i].lastRequestMs) >= 250)
                {
                    sendARPRequest(targetIP);
                    m_arpInFlight[i].lastRequestMs = m_nowMs;
                    if (m_arpInFlight[i].retries < 255)
                        m_arpInFlight[i].retries++;
                }
                return;
            }
        }

        // Allocate new inflight slot.
        QC::usize slot = ARP_INFLIGHT_MAX;
        for (QC::usize i = 0; i < ARP_INFLIGHT_MAX; ++i)
        {
            if (!m_arpInFlight[i].valid)
            {
                slot = i;
                break;
            }
        }
        if (slot == ARP_INFLIGHT_MAX)
            slot = 0;

        m_arpInFlight[slot].valid = true;
        m_arpInFlight[slot].ip = targetIP;
        m_arpInFlight[slot].lastRequestMs = m_nowMs;
        m_arpInFlight[slot].retries = 1;
        sendARPRequest(targetIP);
    }

    void Ethernet::clearArpInFlight(QC::u32 ipAddress)
    {
        for (QC::usize i = 0; i < ARP_INFLIGHT_MAX; ++i)
        {
            if (m_arpInFlight[i].valid && m_arpInFlight[i].ip == ipAddress)
            {
                m_arpInFlight[i].valid = false;
                m_arpInFlight[i].ip = 0;
                m_arpInFlight[i].lastRequestMs = 0;
                m_arpInFlight[i].retries = 0;
            }
        }
    }

    void Ethernet::dropPendingFor(QC::u32 nextHopIp)
    {
        for (QC::usize i = 0; i < PENDING_MAX; ++i)
        {
            if (!m_pending[i].valid)
                continue;
            if (m_pending[i].nextHopIp != nextHopIp)
                continue;

            if (m_pending[i].payload)
                QK::Memory::Heap::instance().free(m_pending[i].payload);
            m_pending[i].payload = nullptr;
            m_pending[i].length = 0;
            m_pending[i].valid = false;
        }
    }

    void Ethernet::sendARPRequest(QC::u32 targetIP)
    {
        ARPPacket arp;
        arp.hardwareType = htons(1); // Ethernet
        arp.protocolType = htons(EtherType::IPv4);
        arp.hardwareAddrLen = 6;
        arp.protocolAddrLen = 4;
        arp.operation = htons(ARPOperation::Request);
        arp.senderMAC = m_mac;
        arp.senderIP = Stack::instance().ip()->address().value;
        memset(&arp.targetMAC, 0, sizeof(MACAddress));
        arp.targetIP = targetIP;

        // Broadcast ARP request
        MACAddress broadcast;
        memset(&broadcast, 0xFF, sizeof(broadcast));
        sendFrame(broadcast, EtherType::ARP, &arp, sizeof(arp));
    }

    void Ethernet::sendARPReply(QC::u32 targetIP, const MACAddress &targetMAC)
    {
        ARPPacket arp;
        arp.hardwareType = htons(1);
        arp.protocolType = htons(EtherType::IPv4);
        arp.hardwareAddrLen = 6;
        arp.protocolAddrLen = 4;
        arp.operation = htons(ARPOperation::Reply);
        arp.senderMAC = m_mac;
        arp.senderIP = Stack::instance().ip()->address().value;
        arp.targetMAC = targetMAC;
        arp.targetIP = targetIP;

        sendFrame(targetMAC, EtherType::ARP, &arp, sizeof(arp));
    }

} // namespace QNet
