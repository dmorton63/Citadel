#include <cassert>
#include <iostream>
#include <vector>

#include "../../kernel/Drivers/XHCI/xhci_hid.h"

using namespace QKDrv::XHCI;

static void addInterface(std::vector<QC::u8> &data, QC::u8 number, QC::u8 subclass,
                         QC::u8 protocol, QC::u8 endpoint, QC::u8 reportLength,
                         QC::u8 deviceClass = 3, QC::u8 alternate = 0)
{
    const QC::u8 descriptors[] = {
        9, 4, number, alternate, 1, deviceClass, subclass, protocol, 0,
        9, 0x21, 0x11, 1, 0, 1, 0x22, reportLength, 0,
        7, 5, endpoint, 3, 8, 0, 10};
    data.insert(data.end(), std::begin(descriptors), std::end(descriptors));
    data[2] = data.size() & 0xFF;
    data[3] = data.size() >> 8;
}

int main()
{
    std::vector<QC::u8> data = {9, 2, 9, 0, 5, 1, 0, 0x80, 50};
    addInterface(data, 0, 1, 1, 0x81, 65);
    for (QC::u8 i = 1; i < 5; ++i)
        addInterface(data, i, 0, 0, 0x81 + i, 20 + i);
    HIDInterfaceSelection selected{};
    assert(selectHIDInterface(data.data(), data.size(), selected));
    assert(selected.interface[2] == 0 && selected.endpoint[2] == 0x81 && selected.hid[7] == 65);

    data = {9, 2, 9, 0, 3, 1, 0, 0x80, 50};
    addInterface(data, 0, 1, 2, 0x81, 94);
    addInterface(data, 1, 0, 0, 0x82, 159);
    addInterface(data, 2, 0, 0, 0x83, 8);
    assert(selectHIDInterface(data.data(), data.size(), selected));
    assert(selected.interface[2] == 0 && selected.interface[7] == 2 && selected.endpoint[2] == 0x81);

    // A keyboard following a generic interface must replace the whole selection.
    addInterface(data, 3, 1, 1, 0x84, 63);
    assert(selectHIDInterface(data.data(), data.size(), selected));
    assert(selected.interface[2] == 3 && selected.endpoint[2] == 0x84 && selected.hid[7] == 63);

    // Do not pair an endpoint from a different interface or alternate setting.
    data = {9, 2, 9, 0, 3, 1, 0, 0x80, 50};
    addInterface(data, 0, 1, 1, 0x01, 63);
    addInterface(data, 1, 0, 0, 0x82, 20, 0xFF);
    addInterface(data, 0, 1, 1, 0x83, 63, 3, 1);
    assert(selectHIDInterface(data.data(), data.size(), selected));
    assert(!selected.interface && !selected.endpoint);

    data = {9, 2, 9, 0, 1, 1, 0, 0x80, 50};
    addInterface(data, 0, 0, 0, 0x81, 74);
    assert(selectHIDInterface(data.data(), data.size(), selected));
    assert(selected.endpoint[2] == 0x81 && selected.hid[7] == 74);
    assert(!selectHIDInterface(data.data(), data.size() - 1, selected));
    data[9] = 0;
    assert(!selectHIDInterface(data.data(), data.size(), selected));

    std::cout << "xHCI composite HID selection tests passed\n";
}
