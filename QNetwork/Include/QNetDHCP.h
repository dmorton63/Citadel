#pragma once

// QNetwork DHCP - Dynamic Host Configuration Protocol (IPv4)
// Namespace: QNet

#include "QCTypes.h"
#include "QNetIP.h"

namespace QNet
{

    struct UDPBinding;

    struct DHCPv4Lease
    {
        IPv4Address address{};
        IPv4Address subnetMask{};
        IPv4Address gateway{};
        IPv4Address dnsServer{};
        IPv4Address serverId{};
        QC::u32 leaseTimeSec = 0;
    };

    class DHCPv4Client
    {
    public:
        DHCPv4Client();
        ~DHCPv4Client();

        // Starts the DHCP process (sends DISCOVER and binds UDP/68).
        QC::Status begin();

        // Polls for incoming DHCP packets; returns true once a lease is acquired.
        // Callers are expected to pump the NIC/driver stack externally.
        bool poll(DHCPv4Lease *outLease);

        void reset();

    private:
        enum class State : QC::u8
        {
            Idle = 0,
            DiscoverSent,
            OfferReceived,
            RequestSent,
            Bound,
            Failed,
        };

        State m_state;
        UDPBinding *m_binding;
        QC::u32 m_xid;

        IPv4Address m_offeredAddr;
        IPv4Address m_subnetMask;
        IPv4Address m_gateway;
        IPv4Address m_dnsServer;
        IPv4Address m_serverId;
        QC::u32 m_leaseTimeSec;

        QC::Status sendDiscover();
        QC::Status sendRequest();

        bool handleDhcpPacket(const void *data, QC::usize length, DHCPv4Lease *outLease);
        bool parseOptions(const QC::u8 *options, QC::usize length, QC::u8 *outMsgType);
        bool macLooksValid() const;

        static bool macMatches(const QC::u8 *chaddr16, const QC::u8 mac6[6]);
    };

} // namespace QNet
