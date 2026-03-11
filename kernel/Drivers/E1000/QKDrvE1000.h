#pragma once

// QKDrvE1000 - Intel e1000 NIC driver (minimal)
// Namespace: QKDrv::E1000

#include "QKDrvBase.h"
#include "QArchPCI.h"
#include "QCTypes.h"

namespace QKDrv::E1000
{

    class Controller final : public DriverBase
    {
    public:
        static Controller *probe(QArch::PCIDevice *pciDevice);
        static Controller *instance();

        QC::Status initialize() override;
        void shutdown() override;
        void poll() override;

        const char *name() const override { return "e1000"; }
        ControllerType controllerType() const override { return ControllerType::None; }

        bool isInitialized() const { return m_initialized; }
        const QC::u8 *macAddress() const { return m_mac; }

        void transmit(const void *data, QC::usize length);
        static void transmitCallback(const void *data, QC::usize length);

    private:
        explicit Controller(QArch::PCIDevice *pciDevice);

        QC::u32 readReg(QC::u32 offset) const;
        void writeReg(QC::u32 offset, QC::u32 value);

        void reset();
        void readMACFromRegisters();
        void initRX();
        void initTX();

        QArch::PCIDevice *m_pciDevice;
        QC::VirtAddr m_mmioBase;
        bool m_initialized;
        QC::u8 m_mac[6];

        static constexpr QC::usize RX_DESC_COUNT = 64;
        static constexpr QC::usize TX_DESC_COUNT = 64;
        static constexpr QC::usize RX_BUF_SIZE = 2048;

        struct RxDesc
        {
            QC::u64 addr;
            QC::u16 length;
            QC::u16 csum;
            QC::u8 status;
            QC::u8 errors;
            QC::u16 special;
        } __attribute__((packed));

        struct TxDesc
        {
            QC::u64 addr;
            QC::u16 length;
            QC::u8 cso;
            QC::u8 cmd;
            QC::u8 status;
            QC::u8 css;
            QC::u16 special;
        } __attribute__((packed));

        RxDesc *m_rxRing;
        QC::PhysAddr m_rxRingPhys;
        QC::u8 *m_rxBufVirt[RX_DESC_COUNT];
        QC::PhysAddr m_rxBufPhys[RX_DESC_COUNT];
        QC::u32 m_rxTail;

        TxDesc *m_txRing;
        QC::PhysAddr m_txRingPhys;
        QC::u8 *m_txBufVirt[TX_DESC_COUNT];
        QC::PhysAddr m_txBufPhys[TX_DESC_COUNT];
        QC::u32 m_txTail;

        void setupReceiveAddress();
    };

} // namespace QKDrv::E1000
