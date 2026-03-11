#pragma once

// QNetwork Socket - BSD-style socket API
// Namespace: QNet

#include "QCTypes.h"
#include "QNetIP.h"
#include "QNetTCP.h"
#include "QNetUDP.h"

namespace QNet
{

    enum class SocketType : QC::u8
    {
        Stream,   // TCP
        Datagram, // UDP
        Raw       // Raw IP
    };

    enum class SocketOption : QC::u8
    {
        ReuseAddr,
        KeepAlive,
        NoDelay,
        Broadcast,
        SendTimeout,
        RecvTimeout,
        SendBufferSize,
        RecvBufferSize
    };

    // Socket address
    struct SocketAddress
    {
        IPv4Address address;
        QC::u16 port;
    };

    class Socket
    {
    public:
        Socket(SocketType type);
        ~Socket();

        // Connection
        QC::Status bind(const SocketAddress &addr);
        QC::Status connect(const SocketAddress &addr);
        QC::Status listen(QC::i32 backlog);
        Socket *accept(SocketAddress *clientAddr);

        // Data transfer
        QC::isize send(const void *data, QC::usize length);
        QC::isize recv(void *buffer, QC::usize length);

        // Datagram (UDP)
        QC::isize sendTo(const SocketAddress &dest, const void *data, QC::usize length);
        QC::isize recvFrom(SocketAddress *source, void *buffer, QC::usize length);

        // Control
        QC::Status shutdown(bool read, bool write);
        void close();

        // Options
        QC::Status setOption(SocketOption opt, const void *value, QC::usize length);
        QC::Status getOption(SocketOption opt, void *value, QC::usize *length);

        // Status
        bool isConnected() const { return m_connected; }
        bool isBound() const { return m_bound; }
        bool isListening() const { return m_listening; }
        SocketType type() const { return m_type; }

        // Address info
        SocketAddress localAddress() const { return m_localAddr; }
        SocketAddress remoteAddress() const { return m_remoteAddr; }

    private:
        SocketType m_type;
        bool m_bound;
        bool m_connected;
        bool m_listening;

        SocketAddress m_localAddr;
        SocketAddress m_remoteAddr;

        // Type-specific handles
        TCPConnection *m_tcpConn;
        UDPBinding *m_udpBinding;
    };

} // namespace QNet
