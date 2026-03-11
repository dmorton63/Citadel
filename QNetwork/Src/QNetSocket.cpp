// QNetwork Socket - BSD-style socket API implementation
// Namespace: QNet

#include "QNetSocket.h"
#include "QNetStack.h"
#include "QCMemUtil.h"
#include "QKMemHeap.h"

namespace QNet
{

    Socket::Socket(SocketType type)
        : m_type(type), m_bound(false), m_connected(false), m_listening(false),
          m_tcpConn(nullptr), m_udpBinding(nullptr)
    {
        m_localAddr.address.value = 0;
        m_localAddr.port = 0;
        m_remoteAddr.address.value = 0;
        m_remoteAddr.port = 0;
    }

    Socket::~Socket()
    {
        close();
    }

    QC::Status Socket::bind(const SocketAddress &addr)
    {
        if (m_bound)
            return QC::Status::Error;

        m_localAddr = addr;

        switch (m_type)
        {
        case SocketType::Stream:
            // TCP binding happens on listen/connect
            break;

        case SocketType::Datagram:
            m_udpBinding = Stack::instance().udp()->bind(addr.port);
            if (!m_udpBinding)
                return QC::Status::Busy;
            break;

        case SocketType::Raw:
            // Raw sockets not fully implemented
            break;
        }

        m_bound = true;
        return QC::Status::Success;
    }

    QC::Status Socket::connect(const SocketAddress &addr)
    {
        if (m_connected)
            return QC::Status::Error;

        m_remoteAddr = addr;

        switch (m_type)
        {
        case SocketType::Stream:
            m_tcpConn = Stack::instance().tcp()->connect(addr.address, addr.port);
            if (!m_tcpConn)
                return QC::Status::Error;

            // Wait for connection (simplified - real impl would be async)
            // For now, assume connected after SYN sent
            m_localAddr.address = Stack::instance().ip()->address();
            m_localAddr.port = m_tcpConn->localPort;
            break;

        case SocketType::Datagram:
            // UDP "connect" just sets default destination
            if (!m_bound)
            {
                m_localAddr.address = Stack::instance().ip()->address();
                m_localAddr.port = 0; // Will use ephemeral
                m_udpBinding = Stack::instance().udp()->bind(0);
                m_bound = true;
            }
            break;

        case SocketType::Raw:
            break;
        }

        m_connected = true;
        return QC::Status::Success;
    }

    QC::Status Socket::listen(QC::i32 backlog)
    {
        (void)backlog; // Backlog not implemented yet

        if (!m_bound || m_type != SocketType::Stream)
            return QC::Status::Error;

        m_tcpConn = Stack::instance().tcp()->listen(m_localAddr.port);
        if (!m_tcpConn)
            return QC::Status::Error;

        m_listening = true;
        return QC::Status::Success;
    }

    Socket *Socket::accept(SocketAddress *clientAddr)
    {
        if (!m_listening || m_type != SocketType::Stream)
            return nullptr;

        // In a real implementation, we'd wait for incoming connection
        // and return a new socket for the connection.
        // For now, this is a placeholder.

        // Check if we have a pending connection
        if (m_tcpConn && m_tcpConn->state == TCPState::Established)
        {
            auto *newSocket = new Socket(SocketType::Stream);
            newSocket->m_tcpConn = m_tcpConn;
            newSocket->m_connected = true;
            newSocket->m_bound = true;
            newSocket->m_localAddr = m_localAddr;
            newSocket->m_remoteAddr.address = m_tcpConn->remoteAddr;
            newSocket->m_remoteAddr.port = m_tcpConn->remotePort;

            if (clientAddr)
            {
                clientAddr->address = m_tcpConn->remoteAddr;
                clientAddr->port = m_tcpConn->remotePort;
            }

            // Create new listening socket
            m_tcpConn = Stack::instance().tcp()->listen(m_localAddr.port);

            return newSocket;
        }

        return nullptr;
    }

    QC::isize Socket::send(const void *data, QC::usize length)
    {
        if (!m_connected)
            return -1;

        switch (m_type)
        {
        case SocketType::Stream:
            if (!m_tcpConn)
                return -1;
            return Stack::instance().tcp()->send(m_tcpConn, data, length);

        case SocketType::Datagram:
            return sendTo(m_remoteAddr, data, length);

        default:
            return -1;
        }
    }

    QC::isize Socket::recv(void *buffer, QC::usize length)
    {
        if (!m_connected && m_type == SocketType::Stream)
            return -1;

        switch (m_type)
        {
        case SocketType::Stream:
            if (!m_tcpConn)
                return -1;
            return Stack::instance().tcp()->receive(m_tcpConn, buffer, length);

        case SocketType::Datagram:
            return recvFrom(nullptr, buffer, length);

        default:
            return -1;
        }
    }

    QC::isize Socket::sendTo(const SocketAddress &dest, const void *data, QC::usize length)
    {
        if (m_type != SocketType::Datagram)
            return -1;

        if (!m_bound)
        {
            m_localAddr.address = Stack::instance().ip()->address();
            m_localAddr.port = 0;
            m_udpBinding = Stack::instance().udp()->bind(0);
            if (m_udpBinding)
                m_bound = true;
        }

        QC::u16 srcPort = m_localAddr.port;
        if (srcPort == 0 && m_udpBinding)
            srcPort = m_udpBinding->port;

        QC::Status status = Stack::instance().udp()->send(
            dest.address, dest.port, srcPort, data, length);

        if (status != QC::Status::Success)
            return -1;

        return static_cast<QC::isize>(length);
    }

    QC::isize Socket::recvFrom(SocketAddress *source, void *buffer, QC::usize length)
    {
        if (m_type != SocketType::Datagram || !m_udpBinding)
            return -1;

        IPv4Address srcAddr;
        QC::u16 srcPort;

        QC::isize result = Stack::instance().udp()->receive(
            m_udpBinding, buffer, length, &srcAddr, &srcPort);

        if (result > 0 && source)
        {
            source->address = srcAddr;
            source->port = srcPort;
        }

        return result;
    }

    QC::Status Socket::shutdown(bool read, bool write)
    {
        (void)read;
        (void)write;

        // Simplified shutdown
        if (m_type == SocketType::Stream && m_tcpConn)
        {
            Stack::instance().tcp()->close(m_tcpConn);
            m_tcpConn = nullptr;
            m_connected = false;
        }

        return QC::Status::Success;
    }

    void Socket::close()
    {
        switch (m_type)
        {
        case SocketType::Stream:
            if (m_tcpConn)
            {
                Stack::instance().tcp()->close(m_tcpConn);
                m_tcpConn = nullptr;
            }
            break;

        case SocketType::Datagram:
            if (m_udpBinding)
            {
                Stack::instance().udp()->unbind(m_udpBinding);
                m_udpBinding = nullptr;
            }
            break;

        default:
            break;
        }

        m_bound = false;
        m_connected = false;
        m_listening = false;
    }

    QC::Status Socket::setOption(SocketOption opt, const void *value, QC::usize length)
    {
        (void)opt;
        (void)value;
        (void)length;

        // Socket options not fully implemented
        return QC::Status::NotSupported;
    }

    QC::Status Socket::getOption(SocketOption opt, void *value, QC::usize *length)
    {
        (void)opt;
        (void)value;
        (void)length;

        // Socket options not fully implemented
        return QC::Status::NotSupported;
    }

} // namespace QNet
