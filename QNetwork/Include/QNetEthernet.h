#pragma once

// QNetwork Ethernet - Ethernet layer
// Namespace: QNet

#include "QCTypes.h"

namespace QNet
{

    // MAC address
    struct MACAddress
    {
        QC::u8 bytes[6];

        bool operator==(const MACAddress &other) const;
        bool isBroadcast() const;
        bool isMulticast() const;
    };

    // Ethernet header
    struct EthernetHeader
    {
        MACAddress destination;
        MACAddress source;
        QC::u16 etherType;
    } __attribute__((packed));

    // EtherTypes
    namespace EtherType
    {
        constexpr QC::u16 IPv4 = 0x0800;
        constexpr QC::u16 ARP = 0x0806;
        constexpr QC::u16 IPv6 = 0x86DD;
    }

    class Ethernet
    {
    public:
        Ethernet();
        ~Ethernet();

        void initialize();

        // Periodic maintenance (aging/retries). `nowMs` is a monotonic millisecond counter.
        void poll(QC::u64 nowMs);

        // MAC address
        void setMACAddress(const MACAddress &mac) { m_mac = mac; }
        const MACAddress &macAddress() const { return m_mac; }

        // Packet handling
        void receiveFrame(const void *data, QC::usize length);
        void sendFrame(const MACAddress &dest, QC::u16 etherType,
                       const void *payload, QC::usize length);

        // ARP
        bool resolveMAC(QC::u32 ipAddress, MACAddress *mac);
        void updateARPCache(QC::u32 ipAddress, const MACAddress &mac);

        struct ARPCacheEntryView
        {
            QC::u32 ip;
            MACAddress mac;
            QC::u64 timestamp;
        };

        QC::usize copyARPCache(ARPCacheEntryView *out, QC::usize max) const;

        // Pending L3 packets (used while ARP resolution is in progress)
        void queuePendingPacket(QC::u32 nextHopIp, QC::u16 etherType,
                                const void *payload, QC::usize length);

    private:
        MACAddress m_mac;

        QC::u64 m_nowMs = 0;

        // ARP cache
        static constexpr QC::usize ARP_CACHE_SIZE = 64;
        struct ARPEntry
        {
            QC::u32 ip;
            MACAddress mac;
            QC::u64 timestamp;
            bool valid;
        };
        ARPEntry m_arpCache[ARP_CACHE_SIZE];

        // Best-effort ARP retry tracking.
        static constexpr QC::usize ARP_INFLIGHT_MAX = 16;
        struct ArpInFlight
        {
            bool valid;
            QC::u32 ip;
            QC::u64 lastRequestMs;
            QC::u8 retries;
        };
        ArpInFlight m_arpInFlight[ARP_INFLIGHT_MAX];

        static constexpr QC::usize PENDING_MAX = 8;
        struct PendingPacket
        {
            bool valid;
            QC::u32 nextHopIp;
            QC::u16 etherType;
            QC::u8 *payload;
            QC::usize length;
        };
        PendingPacket m_pending[PENDING_MAX];

        // ARP handling
        void handleARP(const void *data, QC::usize length);
        void sendARPRequest(QC::u32 targetIP);
        void sendARPReply(QC::u32 targetIP, const MACAddress &targetMAC);

        void flushPendingFor(QC::u32 nextHopIp, const MACAddress &mac);

        void noteArpRequest(QC::u32 targetIP);
        void clearArpInFlight(QC::u32 ipAddress);
        void dropPendingFor(QC::u32 nextHopIp);
    };

} // namespace QNet
