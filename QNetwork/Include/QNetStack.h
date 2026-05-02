#pragma once

// QNetwork Stack - Network stack manager
// Namespace: QNet

#include "QCTypes.h"

namespace QNet
{

    class Ethernet;
    class IP;
    class TCP;
    class UDP;
    class Socket;

    enum class Protocol : QC::u8
    {
        ICMP = 1,
        TCP = 6,
        UDP = 17
    };

    struct PortAuditEvent
    {
        QC::u64 t_ms = 0;
        QC::u64 code = 0;
        Protocol protocol = Protocol::ICMP;
        QC::u16 port = 0;
        QC::u32 ownerPid = 0;
    };

    class Stack
    {
    public:
        static Stack &instance();

        void initialize();

        // Periodic maintenance (e.g., ARP aging/retries). `nowMs` is monotonic milliseconds.
        void poll(QC::u64 nowMs);

        // Layer access
        Ethernet *ethernet() { return m_ethernet; }
        IP *ip() { return m_ip; }
        TCP *tcp() { return m_tcp; }
        UDP *udp() { return m_udp; }

        // Packet handling
        void receivePacket(const void *data, QC::usize length);
        void transmitPacket(const void *data, QC::usize length);

        // Best-effort port hygiene: close idle TCP listeners/half-open and ephemeral UDP binds.
        QC::usize closeUnusedPorts();

        // Managed port lifecycle hooks used by transport layers.
        bool openManagedPort(Protocol protocol, QC::u16 port, QC::u32 ownerPid);
        bool closeManagedPort(Protocol protocol, QC::u16 port);
        bool isManagedPortOpen(Protocol protocol, QC::u16 port) const;
        QC::usize copyPortAuditEvents(PortAuditEvent *out, QC::usize max) const;

        // NIC driver callback
        static void setTransmitCallback(void (*callback)(const void *, QC::usize));
        static void transmitToNIC(const void *data, QC::usize length);

    private:
        Stack();
        ~Stack();
        Stack(const Stack &) = delete;
        Stack &operator=(const Stack &) = delete;

        Ethernet *m_ethernet;
        IP *m_ip;
        TCP *m_tcp;
        UDP *m_udp;

        static constexpr QC::usize PortAuditLogSize = 64;
        PortAuditEvent m_portAudit[PortAuditLogSize] = {};
        QC::usize m_portAuditHead = 0;
        QC::usize m_portAuditCount = 0;

        void pushPortAudit(QC::u64 code, Protocol protocol, QC::u16 port, QC::u32 ownerPid);
    };

} // namespace QNet
