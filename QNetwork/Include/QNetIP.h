#pragma once

// QNetwork IP - Internet Protocol layer
// Namespace: QNet

#include "QCTypes.h"

namespace QNet
{

    // IPv4 address
    struct IPv4Address
    {
        union
        {
            QC::u32 value;
            QC::u8 octets[4];
        };

        bool operator==(const IPv4Address &other) const { return value == other.value; }
        bool isBroadcast() const { return value == 0xFFFFFFFF; }
        bool isMulticast() const { return (octets[0] & 0xF0) == 0xE0; }
        bool isLoopback() const { return octets[0] == 127; }
    };

    // IPv4 header
    struct IPv4Header
    {
        QC::u8 versionIHL;
        QC::u8 tos;
        QC::u16 totalLength;
        QC::u16 identification;
        QC::u16 flagsFragment;
        QC::u8 ttl;
        QC::u8 protocol;
        QC::u16 headerChecksum;
        IPv4Address source;
        IPv4Address destination;
    } __attribute__((packed));

    // ICMP header
    struct ICMPHeader
    {
        QC::u8 type;
        QC::u8 code;
        QC::u16 checksum;
        QC::u32 rest;
    } __attribute__((packed));

    class IP
    {
    public:
        IP();
        ~IP();

        void initialize();

        // Configuration
        void setAddress(IPv4Address addr) { m_address = addr; }
        void setSubnetMask(IPv4Address mask) { m_subnetMask = mask; }
        void setGateway(IPv4Address gateway) { m_gateway = gateway; }
        void setDnsServer(IPv4Address dns) { m_dnsServer = dns; }

        IPv4Address address() const { return m_address; }
        IPv4Address subnetMask() const { return m_subnetMask; }
        IPv4Address gateway() const { return m_gateway; }
        IPv4Address dnsServer() const { return m_dnsServer; }

        // Packet handling
        void receivePacket(const void *data, QC::usize length);
        void sendPacket(IPv4Address dest, QC::u8 protocol,
                        const void *payload, QC::usize length);

        // ICMP
        void sendICMP(IPv4Address dest, QC::u8 type, QC::u8 code,
                      const void *payload, QC::usize length);

        struct IcmpEchoReply
        {
            IPv4Address source;
            QC::u32 rest;
            QC::usize payloadLen;
        };

        void clearIcmpEchoReplies();
        bool popIcmpEchoReply(IcmpEchoReply *out);

        // Routing
        bool isLocal(IPv4Address addr) const;
        IPv4Address nextHop(IPv4Address dest) const;

        // Checksum
        static QC::u16 checksum(const void *data, QC::usize length);

    private:
        IPv4Address m_address;
        IPv4Address m_subnetMask;
        IPv4Address m_gateway;
        IPv4Address m_dnsServer;
        QC::u16 m_identification;

        static constexpr QC::usize ICMP_ECHO_REPLY_MAX = 8;
        IcmpEchoReply m_icmpEchoReplies[ICMP_ECHO_REPLY_MAX];
        QC::usize m_icmpEchoHead = 0;
        QC::usize m_icmpEchoTail = 0;

        void handleICMP(IPv4Address source, const void *data, QC::usize length);
    };

} // namespace QNet
