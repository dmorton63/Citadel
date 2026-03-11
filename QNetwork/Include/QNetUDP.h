#pragma once

// QNetwork UDP - User Datagram Protocol
// Namespace: QNet

#include "QCTypes.h"
#include "QNetIP.h"

namespace QNet
{

    // UDP header
    struct UDPHeader
    {
        QC::u16 sourcePort;
        QC::u16 destPort;
        QC::u16 length;
        QC::u16 checksum;
    } __attribute__((packed));

    // UDP binding
    struct UDPBinding
    {
        QC::u16 port;
        bool active;

        // Receive queue
        struct Datagram
        {
            IPv4Address source;
            QC::u16 sourcePort;
            QC::u8 *data;
            QC::usize length;
            Datagram *next;
        };
        Datagram *recvQueue;
    };

    class UDP
    {
    public:
        UDP();
        ~UDP();

        void initialize();

        // Port management
        UDPBinding *bind(QC::u16 port);
        void unbind(UDPBinding *binding);

        // Data transfer
        QC::Status send(IPv4Address dest, QC::u16 destPort, QC::u16 sourcePort,
                        const void *data, QC::usize length);
        QC::isize receive(UDPBinding *binding, void *buffer, QC::usize length,
                          IPv4Address *source, QC::u16 *sourcePort);

        // Packet handling
        void receivePacket(IPv4Address source, const void *data, QC::usize length);

    private:
        UDPBinding *findBinding(QC::u16 port);
        QC::u16 allocatePort();
        QC::u16 calculateChecksum(IPv4Address srcAddr, IPv4Address destAddr,
                                  const void *packet, QC::usize length);

        static constexpr QC::usize MAX_BINDINGS = 256;
        UDPBinding *m_bindings[MAX_BINDINGS];
        QC::u16 m_nextPort;
    };

} // namespace QNet
