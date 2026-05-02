// QNetwork Stack - Network stack manager implementation
// Namespace: QNet

#include "QNetStack.h"
#include "QNetEthernet.h"
#include "QNetIP.h"
#include "QNetTCP.h"
#include "QNetUDP.h"
#include "QKRuntimeRegistries.h"
#include "QKMsgBus.h"

namespace QNet
{

    namespace
    {
        static constexpr QC::u64 kPortAuditOpen = 0x504F504EULL;     // POPN
        static constexpr QC::u64 kPortAuditClose = 0x504F434CULL;    // POCL
        static constexpr QC::u64 kPortAuditReject = 0x504F524AULL;   // PORJ
        static constexpr QC::u64 kPortAuditAutoclose = 0x504F4155ULL; // POAU

        static QK::Runtime::PortProtocol toRuntimePortProtocol(Protocol protocol)
        {
            switch (protocol)
            {
            case Protocol::TCP:
                return QK::Runtime::PortProtocol::TCP;
            case Protocol::UDP:
                return QK::Runtime::PortProtocol::UDP;
            default:
                return QK::Runtime::PortProtocol::Unknown;
            }
        }

        static QC::u64 nextPortAuditTick()
        {
            static QC::u64 s_tick = 0;
            return ++s_tick;
        }

        static void publishPortAuditEvent(QC::u64 code, Protocol protocol, QC::u16 port, QC::u32 ownerPid)
        {
            QK::Msg::Envelope *env = QK::Msg::makeEnvelope(QK::Msg::Topic::ScAudit);
            if (!env)
                return;

            env->senderId = ownerPid;
            env->targetId = 0;
            env->param1 = code;
            env->param2 = (static_cast<QC::u64>(static_cast<QC::u8>(protocol)) << 56) |
                          (static_cast<QC::u64>(port) << 32) |
                          static_cast<QC::u64>(ownerPid);

            if (!QK::Msg::Bus::instance().publish(env))
                QK::Msg::release(env);
            else
                QK::Msg::release(env);
        }

        static void publishPortAutoCloseSummary(QC::usize closedCount)
        {
            if (closedCount == 0)
                return;

            QK::Msg::Envelope *env = QK::Msg::makeEnvelope(QK::Msg::Topic::ScAudit);
            if (!env)
                return;

            env->senderId = 0;
            env->targetId = 0;
            env->param1 = kPortAuditAutoclose;
            env->param2 = static_cast<QC::u64>(closedCount);

            if (!QK::Msg::Bus::instance().publish(env))
                QK::Msg::release(env);
            else
                QK::Msg::release(env);
        }
    }

    // Static instance for singleton
    static Stack *s_instance = nullptr;

    Stack &Stack::instance()
    {
        if (!s_instance)
        {
            s_instance = new Stack();
        }
        return *s_instance;
    }

    Stack::Stack()
        : m_ethernet(nullptr), m_ip(nullptr), m_tcp(nullptr), m_udp(nullptr)
    {
        m_portAuditHead = 0;
        m_portAuditCount = 0;
    }

    Stack::~Stack()
    {
        if (m_udp)
            delete m_udp;
        if (m_tcp)
            delete m_tcp;
        if (m_ip)
            delete m_ip;
        if (m_ethernet)
            delete m_ethernet;
    }

    void Stack::initialize()
    {
        // Idempotent init (drivers may call this during probing).
        if (m_ethernet || m_ip || m_tcp || m_udp)
        {
            return;
        }

        // Create protocol layers
        m_ethernet = new Ethernet();
        m_ip = new IP();
        m_tcp = new TCP();
        m_udp = new UDP();

        // Initialize each layer
        m_ethernet->initialize();
        m_ip->initialize();
        m_tcp->initialize();
        m_udp->initialize();
    }

    void Stack::poll(QC::u64 nowMs)
    {
        if (m_ethernet)
        {
            m_ethernet->poll(nowMs);
        }

        if (m_tcp)
        {
            m_tcp->poll(nowMs);
        }
    }

    void Stack::receivePacket(const void *data, QC::usize length)
    {
        // Entry point for incoming packets from NIC driver
        // Goes to Ethernet layer first
        if (m_ethernet)
        {
            m_ethernet->receiveFrame(data, length);
        }
    }

    void Stack::transmitPacket(const void *data, QC::usize length)
    {
        // Exit point for outgoing packets to NIC driver
        // This should be called by Ethernet layer after framing

        // Forward to NIC callback if registered.
        transmitToNIC(data, length);
    }

    QC::usize Stack::closeUnusedPorts()
    {
        QC::usize closed = 0;
        if (m_tcp)
            closed += m_tcp->dropUnusedConnections();
        if (m_udp)
            closed += m_udp->dropEphemeralBindings();
        if (closed != 0)
            pushPortAudit(kPortAuditAutoclose, Protocol::ICMP, 0, static_cast<QC::u32>(closed));
        publishPortAutoCloseSummary(closed);
        return closed;
    }

    bool Stack::openManagedPort(Protocol protocol, QC::u16 port, QC::u32 ownerPid)
    {
        const QK::Runtime::PortProtocol runtimeProto = toRuntimePortProtocol(protocol);
        if (runtimeProto == QK::Runtime::PortProtocol::Unknown || port == 0 || ownerPid == 0)
        {
            pushPortAudit(kPortAuditReject, protocol, port, ownerPid);
            publishPortAuditEvent(kPortAuditReject, protocol, port, ownerPid);
            return false;
        }

        auto &regs = QK::Runtime::Registries::instance();
        const QC::u64 nowMs = nextPortAuditTick();
        if (!regs.registerPortWithToken(runtimeProto,
                                        port,
                                        ownerPid,
                                        nullptr,
                                        nowMs,
                                        QK::Runtime::PortState::Opening))
        {
            pushPortAudit(kPortAuditReject, protocol, port, ownerPid);
            publishPortAuditEvent(kPortAuditReject, protocol, port, ownerPid);
            return false;
        }

        if (!regs.transitionPortState(runtimeProto, port, QK::Runtime::PortState::Open))
        {
            (void)regs.unregisterPort(runtimeProto, port);
            pushPortAudit(kPortAuditReject, protocol, port, ownerPid);
            publishPortAuditEvent(kPortAuditReject, protocol, port, ownerPid);
            return false;
        }

        pushPortAudit(kPortAuditOpen, protocol, port, ownerPid);
        publishPortAuditEvent(kPortAuditOpen, protocol, port, ownerPid);

        return true;
    }

    bool Stack::closeManagedPort(Protocol protocol, QC::u16 port)
    {
        const QK::Runtime::PortProtocol runtimeProto = toRuntimePortProtocol(protocol);
        if (runtimeProto == QK::Runtime::PortProtocol::Unknown || port == 0)
            return false;

        auto &regs = QK::Runtime::Registries::instance();
        const QK::Runtime::PortRecord *rec = regs.findPort(runtimeProto, port);
        const QC::u32 ownerPid = rec ? rec->ownerPid : 0;
        (void)regs.transitionPortState(runtimeProto, port, QK::Runtime::PortState::Closing);
        const bool ok = regs.unregisterPort(runtimeProto, port);
        if (ok)
        {
            pushPortAudit(kPortAuditClose, protocol, port, ownerPid);
            publishPortAuditEvent(kPortAuditClose, protocol, port, ownerPid);
        }
        return ok;
    }

    bool Stack::isManagedPortOpen(Protocol protocol, QC::u16 port) const
    {
        const QK::Runtime::PortProtocol runtimeProto = toRuntimePortProtocol(protocol);
        if (runtimeProto == QK::Runtime::PortProtocol::Unknown || port == 0)
            return false;

        const auto *rec = QK::Runtime::Registries::instance().findPort(runtimeProto, port);
        return rec != nullptr;
    }

    void Stack::pushPortAudit(QC::u64 code, Protocol protocol, QC::u16 port, QC::u32 ownerPid)
    {
        const QC::u64 nowMs = nextPortAuditTick();
        const QC::usize idx = (m_portAuditHead + m_portAuditCount) % PortAuditLogSize;
        m_portAudit[idx].t_ms = nowMs;
        m_portAudit[idx].code = code;
        m_portAudit[idx].protocol = protocol;
        m_portAudit[idx].port = port;
        m_portAudit[idx].ownerPid = ownerPid;

        if (m_portAuditCount < PortAuditLogSize)
        {
            ++m_portAuditCount;
        }
        else
        {
            m_portAuditHead = (m_portAuditHead + 1) % PortAuditLogSize;
        }
    }

    QC::usize Stack::copyPortAuditEvents(PortAuditEvent *out, QC::usize max) const
    {
        if (!out || max == 0)
            return 0;

        QC::usize n = m_portAuditCount;
        if (n > max)
            n = max;

        for (QC::usize i = 0; i < n; ++i)
        {
            const QC::usize src = (m_portAuditHead + i) % PortAuditLogSize;
            out[i] = m_portAudit[src];
        }
        return n;
    }

    // NIC driver callback registration
    static void (*s_nicTransmitCallback)(const void *, QC::usize) = nullptr;

    void Stack::setTransmitCallback(void (*callback)(const void *, QC::usize))
    {
        s_nicTransmitCallback = callback;
    }

    void Stack::transmitToNIC(const void *data, QC::usize length)
    {
        if (s_nicTransmitCallback)
        {
            s_nicTransmitCallback(data, length);
        }
    }

} // namespace QNet
