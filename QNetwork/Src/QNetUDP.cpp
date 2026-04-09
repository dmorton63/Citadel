// QNetwork UDP - User Datagram Protocol implementation
// Namespace: QNet

#include "QNetUDP.h"
#include "QNetStack.h"
#include "QNetIP.h"
#include "QCMemUtil.h"
#include "QKMemHeap.h"
#include "QKRuntimeRegistries.h"

namespace QNet
{

    // Byte swap utilities
    static inline QC::u16 htons(QC::u16 val)
    {
        return (val >> 8) | (val << 8);
    }

    static inline QC::u16 ntohs(QC::u16 val)
    {
        return htons(val);
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

    UDP::UDP()
        : m_nextPort(49152)
    {
        memset(m_bindings, 0, sizeof(m_bindings));
    }

    UDP::~UDP()
    {
        for (QC::usize i = 0; i < MAX_BINDINGS; i++)
        {
            if (m_bindings[i])
            {
                (void)QK::Runtime::Registries::instance().unregisterPort(QK::Runtime::PortProtocol::UDP, m_bindings[i]->port);
                // Free receive queue
                auto *dgram = m_bindings[i]->recvQueue;
                while (dgram)
                {
                    auto *next = dgram->next;
                    if (dgram->data)
                        QK::Memory::Heap::instance().free(dgram->data);
                    QK::Memory::Heap::instance().free(dgram);
                    dgram = next;
                }
                QK::Memory::Heap::instance().free(m_bindings[i]);
            }
        }
    }

    void UDP::initialize()
    {
        m_nextPort = 49152;
    }

    UDPBinding *UDP::bind(QC::u16 port)
    {
        static constexpr QC::u32 kInternalNetworkOwnerPid = 1;
        auto &regs = QK::Runtime::Registries::instance();
        if (!regs.findProcess(kInternalNetworkOwnerPid))
            return nullptr;

        // Port 0 means "allocate ephemeral port".
        if (port == 0)
        {
            // Try a bounded number of times to find a free ephemeral port.
            QC::u16 candidate = 0;
            for (QC::usize tries = 0; tries < 1024; ++tries)
            {
                candidate = allocatePort();
                if (!findBinding(candidate))
                {
                    port = candidate;
                    break;
                }
            }
            if (port == 0)
                return nullptr;
        }

        // Check if port already bound
        if (findBinding(port))
            return nullptr;

        // Find free slot
        QC::usize slot = MAX_BINDINGS;
        for (QC::usize i = 0; i < MAX_BINDINGS; i++)
        {
            if (!m_bindings[i])
            {
                slot = i;
                break;
            }
        }
        if (slot == MAX_BINDINGS)
            return nullptr;

        // Create binding
        auto *binding = static_cast<UDPBinding *>(QK::Memory::Heap::instance().allocate(sizeof(UDPBinding)));
        if (!binding)
            return nullptr;

        binding->port = port;
        binding->active = true;
        binding->recvQueue = nullptr;

        if (!regs.registerPort(QK::Runtime::PortProtocol::UDP, port, kInternalNetworkOwnerPid))
        {
            QK::Memory::Heap::instance().free(binding);
            return nullptr;
        }

        m_bindings[slot] = binding;

        return binding;
    }

    void UDP::unbind(UDPBinding *binding)
    {
        if (!binding)
            return;

        for (QC::usize i = 0; i < MAX_BINDINGS; i++)
        {
            if (m_bindings[i] == binding)
            {
                // Free receive queue
                auto *dgram = binding->recvQueue;
                while (dgram)
                {
                    auto *next = dgram->next;
                    if (dgram->data)
                        QK::Memory::Heap::instance().free(dgram->data);
                    QK::Memory::Heap::instance().free(dgram);
                    dgram = next;
                }
                (void)QK::Runtime::Registries::instance().unregisterPort(QK::Runtime::PortProtocol::UDP, binding->port);
                QK::Memory::Heap::instance().free(binding);
                m_bindings[i] = nullptr;
                break;
            }
        }
    }

    QC::Status UDP::send(IPv4Address dest, QC::u16 destPort, QC::u16 sourcePort,
                         const void *data, QC::usize length)
    {
        QC::usize packetLen = sizeof(UDPHeader) + length;
        QC::u8 *packet = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(packetLen));
        if (!packet)
            return QC::Status::OutOfMemory;

        auto *header = reinterpret_cast<UDPHeader *>(packet);
        header->sourcePort = htons(sourcePort);
        header->destPort = htons(destPort);
        header->length = htons(static_cast<QC::u16>(packetLen));
        header->checksum = 0; // Checksum optional for UDP over IPv4

        if (length > 0)
        {
            memcpy(packet + sizeof(UDPHeader), data, length);
        }

        // Calculate checksum
        header->checksum = calculateChecksum(Stack::instance().ip()->address(),
                                             dest, packet, packetLen);

        Stack::instance().ip()->sendPacket(dest, static_cast<QC::u8>(Protocol::UDP),
                                           packet, packetLen);

        QK::Memory::Heap::instance().free(packet);

        return QC::Status::Success;
    }

    QC::isize UDP::receive(UDPBinding *binding, void *buffer, QC::usize length,
                           IPv4Address *source, QC::u16 *sourcePort)
    {
        if (!binding || !binding->active)
            return -1;

        // Get first datagram from queue
        auto *dgram = binding->recvQueue;
        if (!dgram)
            return 0; // No data available

        // Remove from queue
        binding->recvQueue = dgram->next;

        // Copy data
        QC::usize toCopy = dgram->length;
        if (toCopy > length)
            toCopy = length;

        memcpy(buffer, dgram->data, toCopy);

        if (source)
            *source = dgram->source;
        if (sourcePort)
            *sourcePort = dgram->sourcePort;

        // Free datagram
        if (dgram->data)
            QK::Memory::Heap::instance().free(dgram->data);
        QK::Memory::Heap::instance().free(dgram);

        return static_cast<QC::isize>(toCopy);
    }

    void UDP::receivePacket(IPv4Address source, const void *data, QC::usize length)
    {
        if (length < sizeof(UDPHeader))
            return;

        const auto *header = static_cast<const UDPHeader *>(data);

        QC::u16 destPort = ntohs(header->destPort);
        QC::u16 srcPort = ntohs(header->sourcePort);
        QC::u16 udpLen = ntohs(header->length);

        if (udpLen < sizeof(UDPHeader) || udpLen > length)
            return;

        // Find binding for this port
        UDPBinding *binding = findBinding(destPort);
        if (!binding || !binding->active)
            return;

        // Extract payload
        const void *payload = static_cast<const QC::u8 *>(data) + sizeof(UDPHeader);
        QC::usize payloadLen = udpLen - sizeof(UDPHeader);

        // Create datagram entry
        auto *dgram = static_cast<UDPBinding::Datagram *>(
            QK::Memory::Heap::instance().allocate(sizeof(UDPBinding::Datagram)));
        if (!dgram)
            return;

        dgram->source = source;
        dgram->sourcePort = srcPort;
        dgram->length = payloadLen;
        dgram->next = nullptr;

        if (payloadLen > 0)
        {
            dgram->data = static_cast<QC::u8 *>(QK::Memory::Heap::instance().allocate(payloadLen));
            if (!dgram->data)
            {
                QK::Memory::Heap::instance().free(dgram);
                return;
            }
            memcpy(dgram->data, payload, payloadLen);
        }
        else
        {
            dgram->data = nullptr;
        }

        // Add to receive queue
        if (!binding->recvQueue)
        {
            binding->recvQueue = dgram;
        }
        else
        {
            auto *tail = binding->recvQueue;
            while (tail->next)
                tail = tail->next;
            tail->next = dgram;
        }
    }

    QC::usize UDP::dropEphemeralBindings()
    {
        QC::usize dropped = 0;
        for (QC::usize i = 0; i < MAX_BINDINGS; ++i)
        {
            UDPBinding *b = m_bindings[i];
            if (!b)
                continue;
            // Keep well-known/service bindings and drop high ephemeral ports.
            if (b->port < 49152)
                continue;
            unbind(b);
            ++dropped;
        }
        return dropped;
    }

    UDPBinding *UDP::findBinding(QC::u16 port)
    {
        for (QC::usize i = 0; i < MAX_BINDINGS; i++)
        {
            if (m_bindings[i] && m_bindings[i]->port == port)
            {
                return m_bindings[i];
            }
        }
        return nullptr;
    }

    QC::u16 UDP::allocatePort()
    {
        QC::u16 port = m_nextPort++;
        if (m_nextPort >= 65535)
            m_nextPort = 49152;
        return port;
    }

    QC::u16 UDP::calculateChecksum(IPv4Address srcAddr, IPv4Address destAddr,
                                   const void *packet, QC::usize length)
    {
        QC::u32 sum = 0;

        // Pseudo-header (RFC 768): src IP, dst IP, zero, protocol, UDP length.
        // We build bytes explicitly to avoid relying on packed struct alignment.
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
        pseudo[9] = static_cast<QC::u8>(Protocol::UDP);
        // UDP length in network byte order (big-endian).
        pseudo[10] = static_cast<QC::u8>((length >> 8) & 0xFF);
        pseudo[11] = static_cast<QC::u8>((length >> 0) & 0xFF);

        checksumAddBytes(sum, pseudo, sizeof(pseudo));

        // UDP packet (header + payload)
        checksumAddBytes(sum, packet, length);

        // Fold
        while (sum >> 16)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        QC::u16 result = static_cast<QC::u16>(~sum);

        // UDP checksum of 0 means no checksum, use 0xFFFF instead.
        if (result == 0)
            result = 0xFFFF;

        // Return in network byte order to match other UDP header fields.
        return htons(result);
    }

} // namespace QNet
