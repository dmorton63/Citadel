// QNetwork TCP - Transmission Control Protocol implementation
// Namespace: QNet

#include "QNetTCP.h"
#include "QNetStack.h"
#include "QNetIP.h"
#include "QCMemUtil.h"
#include "QKMemHeap.h"
#include "QKRuntimeRegistries.h"

namespace QNet
{

    static inline QC::u32 mix32(QC::u32 x)
    {
        // Xorshift32 (non-crypto) for best-effort sequence number mixing.
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    }

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

    static inline void checksumAddBytes(QC::u32 &sum, const void *data, QC::usize length)
    {
        const auto *bytes = static_cast<const QC::u8 *>(data);
        while (length >= 2)
        {
            const QC::u16 word = static_cast<QC::u16>((static_cast<QC::u16>(bytes[0]) << 8) | bytes[1]);
            sum += word;
            bytes += 2;
            length -= 2;
        }

        if (length == 1)
        {
            const QC::u16 word = static_cast<QC::u16>(static_cast<QC::u16>(bytes[0]) << 8);
            sum += word;
        }
    }

    // Buffer sizes
    static constexpr QC::usize DEFAULT_SEND_BUFFER = 8192;
    static constexpr QC::usize DEFAULT_RECV_BUFFER = 8192;
    static constexpr QC::usize DEFAULT_WINDOW = 65535;

    static void clearInFlightTx(TCPConnection *conn)
    {
        if (!conn)
            return;
        if (conn->txInFlightData)
        {
            QK::Memory::Heap::instance().free(conn->txInFlightData);
            conn->txInFlightData = nullptr;
        }
        conn->txInFlightSeq = 0;
        conn->txInFlightLen = 0;
        conn->txInFlightFlags = 0;
        conn->txInFlightLastTxMs = 0;
        conn->txInFlightRtoMs = 0;
        conn->txInFlightRetries = 0;
    }

    static void onAckAdvance(TCPConnection *conn, QC::u32 ackNum)
    {
        if (!conn)
            return;
        if (conn->txInFlightLen == 0)
            return;

        const QC::u32 end = conn->txInFlightSeq + static_cast<QC::u32>(conn->txInFlightLen);
        if (ackNum >= end)
        {
            clearInFlightTx(conn);
        }
    }

    static QC::usize ringWrite(QC::u8 *buf, QC::usize bufSize, QC::usize &tail, QC::usize &count, const QC::u8 *src, QC::usize len)
    {
        if (!buf || bufSize == 0)
            return 0;
        const QC::usize freeSpace = (bufSize > count) ? (bufSize - count) : 0;
        QC::usize toWrite = len;
        if (toWrite > freeSpace)
            toWrite = freeSpace;
        for (QC::usize i = 0; i < toWrite; ++i)
        {
            buf[tail] = src[i];
            tail = (tail + 1) % bufSize;
        }
        count += toWrite;
        return toWrite;
    }

    static QC::usize ringRead(QC::u8 *buf, QC::usize bufSize, QC::usize &head, QC::usize &count, QC::u8 *dst, QC::usize len)
    {
        if (!buf || bufSize == 0)
            return 0;
        QC::usize toRead = len;
        if (toRead > count)
            toRead = count;
        for (QC::usize i = 0; i < toRead; ++i)
        {
            dst[i] = buf[head];
            head = (head + 1) % bufSize;
        }
        count -= toRead;
        return toRead;
    }

    TCP::TCP()
        : m_nextPort(49152) // Start of ephemeral port range
    {
        memset(m_connections, 0, sizeof(m_connections));
        m_isnSeed = 0x12345678u;

        m_eventHead = 0;
        m_eventCount = 0;
        m_nowMs = 0;
    }

    TCP::~TCP()
    {
        for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (m_connections[i])
            {
                (void)QK::Runtime::Registries::instance().unregisterPort(QK::Runtime::PortProtocol::TCP, m_connections[i]->localPort);
                if (m_connections[i]->sendBuffer)
                    QK::Memory::Heap::instance().free(m_connections[i]->sendBuffer);
                if (m_connections[i]->recvBuffer)
                    QK::Memory::Heap::instance().free(m_connections[i]->recvBuffer);
                QK::Memory::Heap::instance().free(m_connections[i]);
            }
        }
    }

    void TCP::initialize()
    {
        m_nextPort = 49152;
        // Non-crypto seed for initial sequence numbers.
        m_isnSeed = 0x12345678u;

        m_eventHead = 0;
        m_eventCount = 0;
        m_nowMs = 0;
    }

    void TCP::poll(QC::u64 nowMs)
    {
        m_nowMs = nowMs;

        // Minimal connect retransmit logic (SYN only).
        // This significantly improves reachability to hosts that drop the first SYN.
        constexpr QC::u8 kMaxSynRetries = 6;
        for (QC::usize i = 0; i < MAX_CONNECTIONS; ++i)
        {
            TCPConnection *conn = m_connections[i];
            if (!conn)
                continue;
            if (conn->state != TCPState::SynSent)
                continue;

            if (conn->synLastTxMs == 0)
            {
                // First observation after connect(): establish baseline.
                conn->synLastTxMs = nowMs;
                continue;
            }

            const QC::u64 elapsed = nowMs - conn->synLastTxMs;
            const QC::u32 rto = (conn->synRtoMs != 0) ? conn->synRtoMs : 250;
            if (elapsed < static_cast<QC::u64>(rto))
                continue;

            if (conn->synRetries >= kMaxSynRetries)
            {
                conn->state = TCPState::Closed;
                continue;
            }

            // Retransmit SYN using the original ISS.
            sendSegment(conn, TCPFlags::SYN, nullptr, 0, conn->iss, 0);
            conn->synRetries++;
            conn->synLastTxMs = nowMs;
            conn->synRtoMs = (rto < 4000) ? (rto * 2) : 4000;
        }

        // Minimal data retransmit logic for one in-flight small segment.
        constexpr QC::u8 kMaxDataRetries = 5;
        for (QC::usize i = 0; i < MAX_CONNECTIONS; ++i)
        {
            TCPConnection *conn = m_connections[i];
            if (!conn)
                continue;
            if (conn->txInFlightLen == 0 || !conn->txInFlightData)
                continue;
            if (conn->state != TCPState::Established && conn->state != TCPState::CloseWait && conn->state != TCPState::FinWait1 && conn->state != TCPState::FinWait2)
                continue;

            if (conn->txInFlightLastTxMs == 0)
            {
                conn->txInFlightLastTxMs = nowMs;
                continue;
            }

            const QC::u64 elapsed = nowMs - conn->txInFlightLastTxMs;
            const QC::u32 rto = (conn->txInFlightRtoMs != 0) ? conn->txInFlightRtoMs : 500;
            if (elapsed < static_cast<QC::u64>(rto))
                continue;

            if (conn->txInFlightRetries >= kMaxDataRetries)
            {
                // Give up: mark closed so callers can observe failure.
                clearInFlightTx(conn);
                conn->state = TCPState::Closed;
                continue;
            }

            // Retransmit with the original sequence.
            sendSegment(conn, conn->txInFlightFlags,
                        conn->txInFlightData,
                        conn->txInFlightLen,
                        conn->txInFlightSeq,
                        conn->recvNext);
            conn->txInFlightRetries++;
            conn->txInFlightLastTxMs = nowMs;
            conn->txInFlightRtoMs = (rto < 4000) ? (rto * 2) : 4000;
        }
    }

    void TCP::pushEvent(TCPEvent::Dir dir, IPv4Address addr,
                        QC::u16 srcPort, QC::u16 dstPort,
                        QC::u8 flags, QC::u32 seq, QC::u32 ack,
                        QC::u16 payloadLen)
    {
        TCPEvent &e = m_events[m_eventHead];
        e.t_ms = m_nowMs;
        e.dir = dir;
        e.addr = addr;
        e.srcPort = srcPort;
        e.dstPort = dstPort;
        e.flags = flags;
        e.seq = seq;
        e.ack = ack;
        e.payloadLen = payloadLen;

        m_eventHead = (m_eventHead + 1) % EVENT_LOG_SIZE;
        if (m_eventCount < EVENT_LOG_SIZE)
            m_eventCount++;
    }

    QC::usize TCP::copyEventLog(TCPEvent *out, QC::usize max) const
    {
        if (!out || max == 0)
            return 0;

        const QC::usize n = (m_eventCount < max) ? m_eventCount : max;
        if (n == 0)
            return 0;

        // Oldest entry is at (head - count).
        QC::usize start = (m_eventHead + EVENT_LOG_SIZE - m_eventCount) % EVENT_LOG_SIZE;
        for (QC::usize i = 0; i < n; ++i)
        {
            out[i] = m_events[(start + i) % EVENT_LOG_SIZE];
        }
        return n;
    }

    QC::usize TCP::copyConnections(TCPConnectionView *out, QC::usize max) const
    {
        if (!out || max == 0)
            return 0;

        QC::usize n = 0;
        for (QC::usize i = 0; i < MAX_CONNECTIONS && n < max; ++i)
        {
            TCPConnection *conn = m_connections[i];
            if (!conn)
                continue;

            TCPConnectionView &v = out[n++];
            memset(&v, 0, sizeof(v));

            v.localAddr = conn->localAddr;
            v.localPort = conn->localPort;
            v.remoteAddr = conn->remoteAddr;
            v.remotePort = conn->remotePort;
            v.state = conn->state;

            v.sendUnacked = conn->sendUnacked;
            v.sendNext = conn->sendNext;
            v.recvNext = conn->recvNext;

            v.synLastTxMs = conn->synLastTxMs;
            v.synRtoMs = conn->synRtoMs;
            v.synRetries = conn->synRetries;

            v.txInFlightSeq = conn->txInFlightSeq;
            v.txInFlightLen = conn->txInFlightLen;
            v.txInFlightRetries = conn->txInFlightRetries;
            v.txInFlightLastTxMs = conn->txInFlightLastTxMs;
            v.txInFlightRtoMs = conn->txInFlightRtoMs;
        }

        return n;
    }

    bool TCP::dropByLocalPort(QC::u16 localPort)
    {
        for (QC::usize i = 0; i < MAX_CONNECTIONS; ++i)
        {
            TCPConnection *conn = m_connections[i];
            if (!conn)
                continue;
            if (conn->localPort != localPort)
                continue;
            drop(conn);
            return true;
        }
        return false;
    }

    QC::usize TCP::dropUnusedConnections()
    {
        QC::usize dropped = 0;
        for (QC::usize i = 0; i < MAX_CONNECTIONS; ++i)
        {
            TCPConnection *conn = m_connections[i];
            if (!conn)
                continue;

            if (conn->state == TCPState::Established || conn->state == TCPState::CloseWait)
                continue;

            drop(conn);
            ++dropped;
        }
        return dropped;
    }

    TCPConnection *TCP::connect(IPv4Address remoteAddr, QC::u16 remotePort)
    {
        static constexpr QC::u32 kInternalNetworkOwnerPid = 1;
        auto &regs = QK::Runtime::Registries::instance();
        if (!regs.findProcess(kInternalNetworkOwnerPid))
            return nullptr;

        // Find free slot
        QC::usize slot = MAX_CONNECTIONS;
        for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (!m_connections[i])
            {
                slot = i;
                break;
            }
        }
        if (slot == MAX_CONNECTIONS)
            return nullptr;

        // Create connection
        auto *conn = static_cast<TCPConnection *>(QK::Memory::Heap::instance().allocate(sizeof(TCPConnection)));
        if (!conn)
            return nullptr;

        memset(conn, 0, sizeof(TCPConnection));
        conn->localAddr = Stack::instance().ip()->address();
        conn->localPort = allocatePort();
        conn->remoteAddr = remoteAddr;
        conn->remotePort = remotePort;
        conn->state = TCPState::SynSent;

        // Allocate buffers
        conn->sendBuffer = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(DEFAULT_SEND_BUFFER));
        conn->sendBufferSize = DEFAULT_SEND_BUFFER;
        conn->recvBuffer = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(DEFAULT_RECV_BUFFER));
        conn->recvBufferSize = DEFAULT_RECV_BUFFER;

        if (!conn->sendBuffer || !conn->recvBuffer)
        {
            if (conn->sendBuffer)
                QK::Memory::Heap::instance().free(conn->sendBuffer);
            if (conn->recvBuffer)
                QK::Memory::Heap::instance().free(conn->recvBuffer);
            QK::Memory::Heap::instance().free(conn);
            return nullptr;
        }

        // Initialize sequence numbers (best-effort non-crypto ISN).
        // Note: commands often call Stack::initialize(), so avoid a fixed first ISN.
        QC::u32 isn = m_isnSeed;
        isn ^= static_cast<QC::u32>(m_nowMs);
        isn ^= static_cast<QC::u32>(m_nowMs >> 32);
        isn ^= remoteAddr.value;
        isn ^= (static_cast<QC::u32>(conn->localPort) << 16) ^ static_cast<QC::u32>(remotePort);
        isn = mix32(isn);
        if (isn == 0)
            isn = 1;
        conn->iss = isn;
        m_isnSeed = isn + 0x01000001u;
        conn->sendUnacked = conn->iss;
        conn->sendNext = conn->iss;
        conn->sendWindow = DEFAULT_WINDOW;
        conn->recvNext = 0;
        conn->recvWindow = DEFAULT_WINDOW;

        conn->synLastTxMs = 0;
        conn->synRtoMs = 250;
        conn->synRetries = 0;

        conn->recvHead = 0;
        conn->recvTail = 0;
        conn->recvCount = 0;

        conn->txInFlightData = nullptr;
        conn->txInFlightSeq = 0;
        conn->txInFlightLen = 0;
        conn->txInFlightFlags = 0;
        conn->txInFlightLastTxMs = 0;
        conn->txInFlightRtoMs = 0;
        conn->txInFlightRetries = 0;

        if (!regs.registerPort(QK::Runtime::PortProtocol::TCP, conn->localPort, kInternalNetworkOwnerPid))
        {
            if (conn->sendBuffer)
                QK::Memory::Heap::instance().free(conn->sendBuffer);
            if (conn->recvBuffer)
                QK::Memory::Heap::instance().free(conn->recvBuffer);
            QK::Memory::Heap::instance().free(conn);
            return nullptr;
        }

        m_connections[slot] = conn;

        // Send SYN
        sendSegment(conn, TCPFlags::SYN, nullptr, 0);
        conn->sendNext++;

        return conn;
    }

    TCPConnection *TCP::listen(QC::u16 port)
    {
        static constexpr QC::u32 kInternalNetworkOwnerPid = 1;
        auto &regs = QK::Runtime::Registries::instance();
        if (!regs.findProcess(kInternalNetworkOwnerPid))
            return nullptr;
        if (regs.findPort(QK::Runtime::PortProtocol::TCP, port))
            return nullptr;

        // Find free slot
        QC::usize slot = MAX_CONNECTIONS;
        for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (!m_connections[i])
            {
                slot = i;
                break;
            }
        }
        if (slot == MAX_CONNECTIONS)
            return nullptr;

        // Create listening connection
        auto *conn = static_cast<TCPConnection *>(QK::Memory::Heap::instance().allocate(sizeof(TCPConnection)));
        if (!conn)
            return nullptr;

        memset(conn, 0, sizeof(TCPConnection));
        conn->localAddr = Stack::instance().ip()->address();
        conn->localPort = port;
        conn->state = TCPState::Listen;

        // Allocate buffers
        conn->sendBuffer = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(DEFAULT_SEND_BUFFER));
        conn->sendBufferSize = DEFAULT_SEND_BUFFER;
        conn->recvBuffer = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(DEFAULT_RECV_BUFFER));
        conn->recvBufferSize = DEFAULT_RECV_BUFFER;

        if (!conn->sendBuffer || !conn->recvBuffer)
        {
            if (conn->sendBuffer)
                QK::Memory::Heap::instance().free(conn->sendBuffer);
            if (conn->recvBuffer)
                QK::Memory::Heap::instance().free(conn->recvBuffer);
            QK::Memory::Heap::instance().free(conn);
            return nullptr;
        }

        conn->recvWindow = DEFAULT_WINDOW;

        conn->recvHead = 0;
        conn->recvTail = 0;
        conn->recvCount = 0;

        conn->txInFlightData = nullptr;
        conn->txInFlightSeq = 0;
        conn->txInFlightLen = 0;
        conn->txInFlightFlags = 0;
        conn->txInFlightLastTxMs = 0;
        conn->txInFlightRtoMs = 0;
        conn->txInFlightRetries = 0;

        if (!regs.registerPort(QK::Runtime::PortProtocol::TCP, conn->localPort, kInternalNetworkOwnerPid))
        {
            if (conn->sendBuffer)
                QK::Memory::Heap::instance().free(conn->sendBuffer);
            if (conn->recvBuffer)
                QK::Memory::Heap::instance().free(conn->recvBuffer);
            QK::Memory::Heap::instance().free(conn);
            return nullptr;
        }

        m_connections[slot] = conn;

        return conn;
    }

    void TCP::drop(TCPConnection *conn)
    {
        if (!conn)
            return;

        clearInFlightTx(conn);

        const bool havePeer = (conn->remoteAddr.value != 0 && conn->remotePort != 0);
        if (havePeer)
        {
            // Best-effort abort behavior:
            // - SynSent: we may never have received anything from the peer; prefer silent drop.
            // - Established/SynReceived: send RST (ACK only if we have a valid recvNext).
            if (conn->state == TCPState::Established || conn->state == TCPState::SynReceived)
            {
                QC::u8 flags = TCPFlags::RST;
                if (conn->recvNext != 0)
                    flags |= TCPFlags::ACK;
                sendSegment(conn, flags, nullptr, 0, conn->sendNext, conn->recvNext);
            }
        }

        conn->state = TCPState::Closed;

        for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (m_connections[i] == conn)
            {
                bool stillUsed = false;
                for (QC::usize j = 0; j < MAX_CONNECTIONS; ++j)
                {
                    if (j == i || !m_connections[j])
                        continue;
                    if (m_connections[j]->localPort == conn->localPort)
                    {
                        stillUsed = true;
                        break;
                    }
                }
                if (!stillUsed)
                    (void)QK::Runtime::Registries::instance().unregisterPort(QK::Runtime::PortProtocol::TCP, conn->localPort);
                if (conn->sendBuffer)
                    QK::Memory::Heap::instance().free(conn->sendBuffer);
                if (conn->recvBuffer)
                    QK::Memory::Heap::instance().free(conn->recvBuffer);
                if (conn->txInFlightData)
                    QK::Memory::Heap::instance().free(conn->txInFlightData);
                QK::Memory::Heap::instance().free(conn);
                m_connections[i] = nullptr;
                break;
            }
        }
    }

    void TCP::close(TCPConnection *conn)
    {
        if (!conn)
            return;

        switch (conn->state)
        {
        case TCPState::Listen:
        case TCPState::SynSent:
            conn->state = TCPState::Closed;
            break;

        case TCPState::SynReceived:
        case TCPState::Established:
            sendSegment(conn, TCPFlags::FIN | TCPFlags::ACK, nullptr, 0);
            conn->sendNext++;
            conn->state = TCPState::FinWait1;
            break;

        case TCPState::CloseWait:
            sendSegment(conn, TCPFlags::FIN | TCPFlags::ACK, nullptr, 0);
            conn->sendNext++;
            conn->state = TCPState::LastAck;
            break;

        default:
            break;
        }

        // If closed, clean up
        if (conn->state == TCPState::Closed)
        {
            clearInFlightTx(conn);
            for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
            {
                if (m_connections[i] == conn)
                {
                    if (conn->sendBuffer)
                        QK::Memory::Heap::instance().free(conn->sendBuffer);
                    if (conn->recvBuffer)
                        QK::Memory::Heap::instance().free(conn->recvBuffer);
                    if (conn->txInFlightData)
                        QK::Memory::Heap::instance().free(conn->txInFlightData);
                    QK::Memory::Heap::instance().free(conn);
                    m_connections[i] = nullptr;
                    break;
                }
            }
        }
    }

    QC::isize TCP::send(TCPConnection *conn, const void *data, QC::usize length)
    {
        if (!conn || conn->state != TCPState::Established)
            return -1;

        if (!data || length == 0)
            return 0;

        // Only support one in-flight tracked data segment for now.
        if (conn->txInFlightLen != 0)
            return 0;

        // For simplicity, send data in one segment (real implementation would segment)
        QC::usize toSend = length;
        if (toSend > conn->sendWindow)
            toSend = conn->sendWindow;

        const QC::u32 seq = conn->sendNext;
        const QC::u8 flags = TCPFlags::PSH | TCPFlags::ACK;
        sendSegment(conn, flags, data, toSend);

        // Track small payloads for retransmit.
        // (We don't segment yet; this is just for simple clients.)
        if (toSend > 0 && toSend <= 1460)
        {
            conn->txInFlightData = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(toSend));
            if (conn->txInFlightData)
            {
                memcpy(conn->txInFlightData, data, toSend);
                conn->txInFlightSeq = seq;
                conn->txInFlightLen = static_cast<QC::u16>(toSend);
                conn->txInFlightFlags = flags;
                conn->txInFlightLastTxMs = m_nowMs;
                conn->txInFlightRtoMs = 500;
                conn->txInFlightRetries = 0;
            }
        }

        conn->sendNext += static_cast<QC::u32>(toSend);

        return static_cast<QC::isize>(toSend);
    }

    QC::isize TCP::receive(TCPConnection *conn, void *buffer, QC::usize length)
    {
        if (!conn || (conn->state != TCPState::Established && conn->state != TCPState::CloseWait))
            return -1;

        if (!buffer || length == 0)
            return 0;
        if (!conn->recvBuffer || conn->recvBufferSize == 0)
            return 0;

        const QC::usize n = ringRead(conn->recvBuffer, conn->recvBufferSize,
                                     conn->recvHead, conn->recvCount,
                                     static_cast<QC::u8 *>(buffer), length);
        return static_cast<QC::isize>(n);
    }

    void TCP::receivePacket(IPv4Address source, const void *data, QC::usize length)
    {
        if (length < sizeof(TCPHeader))
            return;

        const auto *header = static_cast<const TCPHeader *>(data);

        QC::u16 destPort = ntohs(header->destPort);
        QC::u16 srcPort = ntohs(header->sourcePort);
        const QC::u32 seqNum = ntohl(header->seqNumber);
        const QC::u32 ackNum = ntohl(header->ackNumber);

        // Find matching connection
        TCPConnection *conn = findConnection(source, srcPort, destPort);

        // Check for listening socket if no established connection
        if (!conn)
        {
            for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
            {
                if (m_connections[i] &&
                    m_connections[i]->state == TCPState::Listen &&
                    m_connections[i]->localPort == destPort)
                {
                    conn = m_connections[i];
                    break;
                }
            }
        }

        if (!conn)
        {
            // Send RST for unknown connection
            return;
        }

        // Get data offset
        QC::u8 dataOffset = (header->dataOffset >> 4) & 0x0F;
        QC::usize headerLen = dataOffset * 4;
        if (dataOffset < 5)
            return;
        if (headerLen > length)
            return;
        const void *payload = static_cast<const QC::u8 *>(data) + headerLen;
        QC::usize payloadLen = length - headerLen;

        pushEvent(TCPEvent::Dir::Rx, source, srcPort, destPort, header->flags, seqNum, ackNum,
                  static_cast<QC::u16>(payloadLen));

        processSegment(conn, source, header, payload, payloadLen);
    }

    void TCP::sendSegment(TCPConnection *conn, QC::u8 flags,
                          const void *data, QC::usize length)
    {
        sendSegment(conn, flags, data, length, conn->sendNext, conn->recvNext);
    }

    void TCP::sendSegment(TCPConnection *conn, QC::u8 flags,
                          const void *data, QC::usize length,
                          QC::u32 seqNumber, QC::u32 ackNumber)
    {
        // Add a minimal MSS option on SYN to improve compatibility.
        QC::usize optLen = 0;
        if (flags & TCPFlags::SYN)
            optLen = 4;

        const QC::usize headerBytes = sizeof(TCPHeader) + optLen;
        QC::usize segmentLen = headerBytes + length;
        QC::u8 *segment = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(segmentLen));
        if (!segment)
            return;

        auto *header = reinterpret_cast<TCPHeader *>(segment);
        header->sourcePort = htons(conn->localPort);
        header->destPort = htons(conn->remotePort);
        header->seqNumber = htonl(seqNumber);
        header->ackNumber = htonl(ackNumber);
        header->dataOffset = static_cast<QC::u8>(((headerBytes / 4) & 0x0F) << 4);
        header->flags = flags;
        header->window = htons(static_cast<QC::u16>(conn->recvWindow));
        header->checksum = 0;
        header->urgentPointer = 0;

        if (optLen)
        {
            // MSS option: kind=2 len=4 value=1460
            const QC::u16 mss = 1460;
            QC::u8 *opt = segment + sizeof(TCPHeader);
            opt[0] = 2;
            opt[1] = 4;
            opt[2] = static_cast<QC::u8>((mss >> 8) & 0xFF);
            opt[3] = static_cast<QC::u8>((mss >> 0) & 0xFF);
        }

        if (length > 0)
        {
            memcpy(segment + headerBytes, data, length);
        }

        // Calculate checksum with pseudo-header
        header->checksum = calculateChecksum(conn->localAddr, conn->remoteAddr,
                                             segment, segmentLen);

        pushEvent(TCPEvent::Dir::Tx, conn->remoteAddr, conn->localPort, conn->remotePort,
              flags, seqNumber, ackNumber, static_cast<QC::u16>(length));

        Stack::instance().ip()->sendPacket(conn->remoteAddr,
                                           static_cast<QC::u8>(Protocol::TCP),
                                           segment, segmentLen);

        QK::Memory::Heap::instance().free(segment);
    }

    void TCP::processSegment(TCPConnection *conn, IPv4Address sourceAddr, const TCPHeader *header,
                             const void *data, QC::usize length)
    {
        QC::u8 flags = header->flags;
        QC::u32 seqNum = ntohl(header->seqNumber);
        QC::u32 ackNum = ntohl(header->ackNumber);

        if (flags & TCPFlags::RST)
        {
            conn->state = TCPState::Closed;
            return;
        }

        switch (conn->state)
        {
        case TCPState::Listen:
            if (flags & TCPFlags::SYN)
            {
                conn->remoteAddr = sourceAddr;
                conn->remotePort = ntohs(header->sourcePort);
                conn->recvNext = seqNum + 1;
                conn->sendUnacked = 2000;
                conn->sendNext = 2000;

                sendSegment(conn, TCPFlags::SYN | TCPFlags::ACK, nullptr, 0);
                conn->sendNext++;
                conn->state = TCPState::SynReceived;
            }
            break;

        case TCPState::SynSent:
            if ((flags & (TCPFlags::SYN | TCPFlags::ACK)) == (TCPFlags::SYN | TCPFlags::ACK))
            {
                conn->recvNext = seqNum + 1;
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);

                conn->synRetries = 0;
                conn->synLastTxMs = 0;
                conn->synRtoMs = 250;

                sendSegment(conn, TCPFlags::ACK, nullptr, 0);
                conn->state = TCPState::Established;
            }
            break;

        case TCPState::SynReceived:
            if (flags & TCPFlags::ACK)
            {
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);
                conn->state = TCPState::Established;
            }
            break;

        case TCPState::Established:
        {
            if (flags & TCPFlags::ACK)
            {
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);
            }

            bool shouldAck = false;

            // Process data (may be present alongside FIN).
            QC::usize written = 0;
            if (length > 0)
            {
                // Only accept in-order data for now.
                if (seqNum == conn->recvNext && conn->recvBuffer && conn->recvBufferSize)
                {
                    written = ringWrite(conn->recvBuffer, conn->recvBufferSize,
                                        conn->recvTail, conn->recvCount,
                                        static_cast<const QC::u8 *>(data), length);
                    conn->recvNext += static_cast<QC::u32>(written);
                    shouldAck = true;
                }
                else
                {
                    // Out-of-order: ACK current recvNext.
                    shouldAck = true;
                }
            }

            // FIN consumes one sequence number after any payload.
            if (flags & TCPFlags::FIN)
            {
                const QC::u32 finSeq = seqNum + static_cast<QC::u32>(length);
                // Accept FIN only if we've accepted all payload up to FIN.
                if ((length == 0 && seqNum == conn->recvNext) ||
                    (length > 0 && written == length && finSeq == conn->recvNext))
                {
                    conn->recvNext += 1;
                    conn->state = TCPState::CloseWait;
                    shouldAck = true;
                }
                else
                {
                    // FIN out of order; ACK current recvNext.
                    shouldAck = true;
                }
            }

            if (shouldAck)
                sendSegment(conn, TCPFlags::ACK, nullptr, 0);
        }
            break;

        case TCPState::CloseWait:
            if (flags & TCPFlags::ACK)
            {
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);
            }
            break;

        case TCPState::FinWait1:
            if ((flags & TCPFlags::ACK) && (flags & TCPFlags::FIN))
            {
                conn->recvNext = seqNum + 1;
                sendSegment(conn, TCPFlags::ACK, nullptr, 0);
                conn->state = TCPState::TimeWait;
            }
            else if (flags & TCPFlags::ACK)
            {
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);
                conn->state = TCPState::FinWait2;
            }
            else if (flags & TCPFlags::FIN)
            {
                conn->recvNext = seqNum + 1;
                sendSegment(conn, TCPFlags::ACK, nullptr, 0);
                conn->state = TCPState::Closing;
            }
            break;

        case TCPState::FinWait2:
            if (flags & TCPFlags::FIN)
            {
                conn->recvNext = seqNum + 1;
                sendSegment(conn, TCPFlags::ACK, nullptr, 0);
                conn->state = TCPState::TimeWait;
            }
            if (flags & TCPFlags::ACK)
            {
                conn->sendUnacked = ackNum;
                onAckAdvance(conn, ackNum);
            }
            break;

        case TCPState::Closing:
            if (flags & TCPFlags::ACK)
            {
                conn->state = TCPState::TimeWait;
            }
            break;

        case TCPState::LastAck:
            if (flags & TCPFlags::ACK)
            {
                conn->state = TCPState::Closed;
            }
            break;

        default:
            break;
        }

        (void)data;
    }

    QC::u16 TCP::allocatePort()
    {
        QC::u16 port = m_nextPort++;
        if (m_nextPort >= 65535)
            m_nextPort = 49152;
        return port;
    }

    TCPConnection *TCP::findConnection(IPv4Address remoteAddr, QC::u16 remotePort,
                                       QC::u16 localPort)
    {
        for (QC::usize i = 0; i < MAX_CONNECTIONS; i++)
        {
            if (m_connections[i] &&
                m_connections[i]->remoteAddr == remoteAddr &&
                m_connections[i]->remotePort == remotePort &&
                m_connections[i]->localPort == localPort &&
                m_connections[i]->state != TCPState::Listen)
            {
                return m_connections[i];
            }
        }
        return nullptr;
    }

    QC::u16 TCP::calculateChecksum(IPv4Address srcAddr, IPv4Address destAddr,
                                   const void *segment, QC::usize length)
    {
        QC::u32 sum = 0;

        // Pseudo-header (RFC 793): src IP, dst IP, zero, protocol, TCP length.
        QC::u8 pseudo[12];
        pseudo[0] = srcAddr.octets[0];
        pseudo[1] = srcAddr.octets[1];
        pseudo[2] = srcAddr.octets[2];
        pseudo[3] = srcAddr.octets[3];
        pseudo[4] = destAddr.octets[0];
        pseudo[5] = destAddr.octets[1];
        pseudo[6] = destAddr.octets[2];
        pseudo[7] = destAddr.octets[3];
        pseudo[8] = 0;
        pseudo[9] = static_cast<QC::u8>(Protocol::TCP);
        // TCP length in network byte order (big-endian).
        pseudo[10] = static_cast<QC::u8>((length >> 8) & 0xFF);
        pseudo[11] = static_cast<QC::u8>((length >> 0) & 0xFF);

        checksumAddBytes(sum, pseudo, sizeof(pseudo));
        checksumAddBytes(sum, segment, length);

        // Fold
        while (sum >> 16)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        const QC::u16 result = static_cast<QC::u16>(~sum);
        return htons(result);
    }

} // namespace QNet
