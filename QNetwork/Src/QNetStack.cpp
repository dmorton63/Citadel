// QNetwork Stack - Network stack manager implementation
// Namespace: QNet

#include "QNetStack.h"
#include "QNetEthernet.h"
#include "QNetIP.h"
#include "QNetTCP.h"
#include "QNetUDP.h"

namespace QNet
{

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
