#pragma once

// xHCI public driver API
// Namespace: QKDrv::XHCI

#include "QKDrvBase.h"
#include "QArchPCI.h"

namespace QKDrv
{
    namespace XHCI
    {
        class XHCIController : public DriverBase
        {
        public:
            virtual ~XHCIController() = default;

            virtual void setMouseCallback(MouseCallback callback) = 0;
            virtual void setScreenSize(QC::u32 width, QC::u32 height) = 0;

            virtual bool hasTablet() const = 0;
            virtual MouseDriver *tabletDriver() = 0;

            // Relative USB HID mouse (boot protocol)
            virtual bool hasMouse() const = 0;
            virtual MouseDriver *mouseDriver() = 0;

            // Boot-protocol USB HID keyboard
            virtual bool hasKeyboard() const = 0;
            virtual KeyboardDriver *keyboardDriver() = 0;

            virtual void hardwareReset() = 0;
            virtual bool submitTransfer(QC::u8 slotId,
                                        QC::u8 endpointId,
                                        QC::PhysAddr buffer,
                                        QC::u32 length,
                                        QC::u32 trbFlags) = 0;

            virtual void poll() override = 0;
        };

        XHCIController *xhci_init(QArch::PCIDevice *device);
        void xhci_reset(XHCIController *controller);
        bool xhci_submit_transfer(XHCIController *controller,
                                  QC::u8 slotId,
                                  QC::u8 endpointId,
                                  QC::PhysAddr buffer,
                                  QC::u32 length,
                                  QC::u32 trbFlags);
        void xhci_poll_events(XHCIController *controller);
    }
} // namespace QKDrv
