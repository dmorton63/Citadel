#pragma once

// QNetwork TCP - Transmission Control Protocol
// Namespace: QNet

#include "QCTypes.h"
#include "QNetIP.h"

namespace QNet
{

    struct TCPEvent
    {
        enum class Dir : QC::u8
        {
            Tx = 0,
            Rx = 1,
        };

        QC::u64 t_ms;
        Dir dir;
        IPv4Address addr;
        QC::u16 srcPort;
        QC::u16 dstPort;
        QC::u8 flags;
        QC::u32 seq;
        QC::u32 ack;
        QC::u16 payloadLen;
    };

    // TCP header
    struct TCPHeader
    {
        QC::u16 sourcePort;
        QC::u16 destPort;
        QC::u32 seqNumber;
        QC::u32 ackNumber;
        QC::u8 dataOffset;
        QC::u8 flags;
        QC::u16 window;
        QC::u16 checksum;
        QC::u16 urgentPointer;
    } __attribute__((packed));

    // TCP flags
    namespace TCPFlags
    {
        constexpr QC::u8 FIN = 0x01;
        constexpr QC::u8 SYN = 0x02;
        constexpr QC::u8 RST = 0x04;
        constexpr QC::u8 PSH = 0x08;
        constexpr QC::u8 ACK = 0x10;
        constexpr QC::u8 URG = 0x20;
    }

    // TCP states
    enum class TCPState : QC::u8
    {
        Closed,
        Listen,
        SynSent,
        SynReceived,
        Established,
        FinWait1,
        FinWait2,
        CloseWait,
        Closing,
        LastAck,
        TimeWait
    };

    // TCP connection
    struct TCPConnection
    {
        IPv4Address localAddr;
        QC::u16 localPort;
        IPv4Address remoteAddr;
        QC::u16 remotePort;

        TCPState state;

        QC::u32 sendUnacked;
        QC::u32 sendNext;
        QC::u32 sendWindow;

        // Initial send sequence (ISN/ISS) for SYN retransmits.
        QC::u32 iss;

        QC::u32 recvNext;
        QC::u32 recvWindow;

        // Minimal SYN retransmit tracking (connect bring-up).
        QC::u64 synLastTxMs;
        QC::u32 synRtoMs;
        QC::u8 synRetries;

        // Buffers
        QC::u8 *sendBuffer;
        QC::usize sendBufferSize;
        QC::u8 *recvBuffer;
        QC::usize recvBufferSize;

        // Minimal receive ring buffer state
        QC::usize recvHead;
        QC::usize recvTail;
        QC::usize recvCount;

        // Minimal TX retransmit tracking for one small in-flight data segment.
        // Bring-up feature to improve reliability for simple clients like httpget.
        QC::u8 *txInFlightData;
        QC::u32 txInFlightSeq;
        QC::u16 txInFlightLen;
        QC::u8 txInFlightFlags;
        QC::u64 txInFlightLastTxMs;
        QC::u32 txInFlightRtoMs;
        QC::u8 txInFlightRetries;
    };

    // Diagnostics view for active connections.
    // Keep this POD and pointer-free so callers can safely copy snapshots.
    struct TCPConnectionView
    {
        IPv4Address localAddr;
        QC::u16 localPort;
        IPv4Address remoteAddr;
        QC::u16 remotePort;

        TCPState state;

        QC::u32 sendUnacked;
        QC::u32 sendNext;
        QC::u32 recvNext;

        QC::u64 synLastTxMs;
        QC::u32 synRtoMs;
        QC::u8 synRetries;

        QC::u32 txInFlightSeq;
        QC::u16 txInFlightLen;
        QC::u8 txInFlightRetries;
        QC::u64 txInFlightLastTxMs;
        QC::u32 txInFlightRtoMs;
    };

    class TCP
    {
    public:
        TCP();
        ~TCP();

        void initialize();

        // Periodic maintenance (retransmits/timeouts). `nowMs` is monotonic milliseconds.
        void poll(QC::u64 nowMs);

        // Diagnostics: copy the most recent TCP events (oldest -> newest).
        QC::usize copyEventLog(TCPEvent *out, QC::usize max) const;

        // Diagnostics: copy active connection snapshots.
        QC::usize copyConnections(TCPConnectionView *out, QC::usize max) const;

        // Diagnostics/bring-up: drop a connection by local port.
        // Intended for command tools to clean up kept test connections.
        bool dropByLocalPort(QC::u16 localPort);

        // Connection management
        TCPConnection *connect(IPv4Address remoteAddr, QC::u16 remotePort);
        TCPConnection *listen(QC::u16 port);
        void close(TCPConnection *conn);

        // Forcefully frees a connection (diagnostics / early bring-up).
        // Does not perform full FIN/TIME_WAIT handling.
        void drop(TCPConnection *conn);

        // Data transfer
        QC::isize send(TCPConnection *conn, const void *data, QC::usize length);
        QC::isize receive(TCPConnection *conn, void *buffer, QC::usize length);

        // Packet handling
        void receivePacket(IPv4Address source, const void *data, QC::usize length);

    private:
        void pushEvent(TCPEvent::Dir dir, IPv4Address addr,
                       QC::u16 srcPort, QC::u16 dstPort,
                       QC::u8 flags, QC::u32 seq, QC::u32 ack,
                       QC::u16 payloadLen);

        void sendSegment(TCPConnection *conn, QC::u8 flags,
                         const void *data, QC::usize length);
        void sendSegment(TCPConnection *conn, QC::u8 flags,
                         const void *data, QC::usize length,
                         QC::u32 seqNumber, QC::u32 ackNumber);
        void processSegment(TCPConnection *conn, IPv4Address sourceAddr, const TCPHeader *header,
                            const void *data, QC::usize length);

        QC::u16 allocatePort();
        TCPConnection *findConnection(IPv4Address remoteAddr, QC::u16 remotePort,
                                      QC::u16 localPort);
        QC::u16 calculateChecksum(IPv4Address srcAddr, IPv4Address destAddr,
                                  const void *segment, QC::usize length);

        static constexpr QC::usize MAX_CONNECTIONS = 256;
        TCPConnection *m_connections[MAX_CONNECTIONS];
        QC::u16 m_nextPort;
        QC::u32 m_isnSeed;

        static constexpr QC::usize EVENT_LOG_SIZE = 32;
        TCPEvent m_events[EVENT_LOG_SIZE];
        QC::usize m_eventHead;
        QC::usize m_eventCount;
        QC::u64 m_nowMs;
    };

} // namespace QNet
