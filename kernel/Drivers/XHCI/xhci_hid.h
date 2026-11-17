#pragma once

#include "QCTypes.h"

namespace QKDrv::XHCI
{
    struct HIDInterfaceSelection
    {
        const QC::u8 *interface = nullptr;
        const QC::u8 *endpoint = nullptr;
        const QC::u8 *hid = nullptr;
    };

    inline bool selectHIDInterface(const QC::u8 *data, QC::u16 length, HIDInterfaceSelection &selected)
    {
        selected = {};
        if (length < 9 || data[0] < 9 || data[1] != 2 || data[0] > length)
            return false;
        const QC::u16 total = data[2] | (static_cast<QC::u16>(data[3]) << 8);
        if (total < data[0])
            return false;
        const QC::u16 limit = total < length ? total : length;
        HIDInterfaceSelection current{};
        unsigned selectedPriority = 0;
        auto consider = [&]()
        {
            if (!current.interface || !current.endpoint)
                return;
            const bool boot = current.interface[6] == 1;
            const unsigned protocol = current.interface[7];
            const unsigned priority = boot && protocol == 1 ? 3 : boot && protocol == 2 ? 2 : 1;
            if (priority > selectedPriority)
            {
                selected = current;
                selectedPriority = priority;
            }
        };
        QC::u16 offset = data[0];
        while (offset < limit)
        {
            if (limit - offset < 2)
                return false;
            const QC::u8 size = data[offset];
            const QC::u8 type = data[offset + 1];
            if (size < 2 || size > limit - offset)
                return false;
            const QC::u8 *descriptor = data + offset;
            if (type == 4)
            {
                if (size < 9)
                    return false;
                consider();
                current = {};
                if (descriptor[5] == 3 && descriptor[3] == 0)
                    current.interface = descriptor;
            }
            else if (type == 5 && current.interface)
            {
                if (size < 7)
                    return false;
                if (!current.endpoint && (descriptor[3] & 3) == 3 && (descriptor[2] & 0x80))
                    current.endpoint = descriptor;
            }
            else if (type == 0x21 && current.interface)
            {
                if (size < 9)
                    return false;
                current.hid = descriptor;
            }
            offset += size;
        }
        consider();
        return true;
    }
}
