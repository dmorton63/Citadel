#pragma once

// QNetwork DNS - Domain Name System (minimal client)
// Namespace: QNet

#include "QCTypes.h"
#include "QNetIP.h"

namespace QNet
{

    struct UDPBinding;

    class DNSClient
    {
    public:
        DNSClient();
        ~DNSClient();

        // Sends a single A-record query for `name` to `server`.
        QC::Status begin(IPv4Address server, const char *name, QC::u16 txid);

        // Returns true once an IPv4 A answer is received.
        bool poll(IPv4Address *outAddress);

        void reset();

    private:
        UDPBinding *m_binding;
        IPv4Address m_server;
        QC::u16 m_txid;
        bool m_active;

        QC::Status sendQuery(const char *name);

        static bool skipName(const QC::u8 *msg, QC::usize msgLen, QC::usize &offset);
    };

} // namespace QNet
