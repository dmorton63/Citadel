#pragma once

#include "QCTypes.h"

namespace QKDrv::Input
{
    inline constexpr QC::u32 kMouseSensitivityPercentMin = 10;
    inline constexpr QC::u32 kMouseSensitivityPercentMax = 400;
    inline constexpr QC::u32 kMouseSensitivityPercentDefault = 100;

    inline QC::u32 clampMouseSensitivityPercent(QC::u32 percent)
    {
        if (percent < kMouseSensitivityPercentMin)
            return kMouseSensitivityPercentMin;
        if (percent > kMouseSensitivityPercentMax)
            return kMouseSensitivityPercentMax;
        return percent;
    }

    inline QC::u32 &mouseSensitivityPercentStorage()
    {
        static QC::u32 percent = kMouseSensitivityPercentDefault;
        return percent;
    }

    inline QC::u32 mouseSensitivityPercent()
    {
        return mouseSensitivityPercentStorage();
    }

    inline void setMouseSensitivityPercent(QC::u32 percent)
    {
        mouseSensitivityPercentStorage() = clampMouseSensitivityPercent(percent);
    }

    inline float mouseSensitivityScale()
    {
        return static_cast<float>(mouseSensitivityPercent()) / 100.0f;
    }

    inline constexpr QC::u32 kMouseUsbRelativePercentMin = 25;
    inline constexpr QC::u32 kMouseUsbRelativePercentMax = 400;
    inline constexpr QC::u32 kMouseUsbRelativePercentDefault = 100;

    inline constexpr QC::u32 kMousePs2RelativePercentMin = 25;
    inline constexpr QC::u32 kMousePs2RelativePercentMax = 400;
    inline constexpr QC::u32 kMousePs2RelativePercentDefault = 100;

    inline constexpr QC::u32 kMouseWheelLinesMin = 1;
    inline constexpr QC::u32 kMouseWheelLinesMax = 16;
    inline constexpr QC::u32 kMouseWheelLinesDefault = 3;

    inline QC::u32 clampMouseUsbRelativePercent(QC::u32 percent)
    {
        if (percent < kMouseUsbRelativePercentMin)
            return kMouseUsbRelativePercentMin;
        if (percent > kMouseUsbRelativePercentMax)
            return kMouseUsbRelativePercentMax;
        return percent;
    }

    inline QC::u32 clampMousePs2RelativePercent(QC::u32 percent)
    {
        if (percent < kMousePs2RelativePercentMin)
            return kMousePs2RelativePercentMin;
        if (percent > kMousePs2RelativePercentMax)
            return kMousePs2RelativePercentMax;
        return percent;
    }

    inline QC::u32 clampMouseWheelLines(QC::u32 lines)
    {
        if (lines < kMouseWheelLinesMin)
            return kMouseWheelLinesMin;
        if (lines > kMouseWheelLinesMax)
            return kMouseWheelLinesMax;
        return lines;
    }

    inline QC::u32 &mouseUsbRelativePercentStorage()
    {
        static QC::u32 percent = kMouseUsbRelativePercentDefault;
        return percent;
    }

    inline QC::u32 &mousePs2RelativePercentStorage()
    {
        static QC::u32 percent = kMousePs2RelativePercentDefault;
        return percent;
    }

    inline QC::u32 &mouseWheelLinesStorage()
    {
        static QC::u32 lines = kMouseWheelLinesDefault;
        return lines;
    }

    inline bool &mouseInvertWheelStorage()
    {
        static bool invert = false;
        return invert;
    }

    inline QC::u32 mouseUsbRelativePercent()
    {
        return mouseUsbRelativePercentStorage();
    }

    inline QC::u32 mousePs2RelativePercent()
    {
        return mousePs2RelativePercentStorage();
    }

    inline QC::u32 mouseWheelLines()
    {
        return mouseWheelLinesStorage();
    }

    inline bool mouseInvertWheel()
    {
        return mouseInvertWheelStorage();
    }

    inline void setMouseUsbRelativePercent(QC::u32 percent)
    {
        mouseUsbRelativePercentStorage() = clampMouseUsbRelativePercent(percent);
    }

    inline void setMousePs2RelativePercent(QC::u32 percent)
    {
        mousePs2RelativePercentStorage() = clampMousePs2RelativePercent(percent);
    }

    inline void setMouseWheelLines(QC::u32 lines)
    {
        mouseWheelLinesStorage() = clampMouseWheelLines(lines);
    }

    inline void setMouseInvertWheel(bool invert)
    {
        mouseInvertWheelStorage() = invert;
    }

    inline float mouseUsbRelativeScale()
    {
        return static_cast<float>(mouseUsbRelativePercent()) / 100.0f;
    }

    inline float mousePs2RelativeScale()
    {
        return static_cast<float>(mousePs2RelativePercent()) / 100.0f;
    }

    inline QC::i32 applyMouseWheelBehavior(QC::i32 wheelDelta)
    {
        QC::i32 adjusted = wheelDelta * static_cast<QC::i32>(mouseWheelLines());
        if (mouseInvertWheel())
            adjusted = -adjusted;
        return adjusted;
    }

    inline constexpr QC::u32 kKeyboardRepeatDelayMsMin = 100;
    inline constexpr QC::u32 kKeyboardRepeatDelayMsMax = 2000;
    inline constexpr QC::u32 kKeyboardRepeatDelayMsDefault = 400;

    inline constexpr QC::u32 kKeyboardRepeatIntervalMsMin = 10;
    inline constexpr QC::u32 kKeyboardRepeatIntervalMsMax = 250;
    inline constexpr QC::u32 kKeyboardRepeatIntervalMsDefault = 33;

    inline QC::u32 clampKeyboardRepeatDelayMs(QC::u32 delayMs)
    {
        if (delayMs < kKeyboardRepeatDelayMsMin)
            return kKeyboardRepeatDelayMsMin;
        if (delayMs > kKeyboardRepeatDelayMsMax)
            return kKeyboardRepeatDelayMsMax;
        return delayMs;
    }

    inline QC::u32 clampKeyboardRepeatIntervalMs(QC::u32 intervalMs)
    {
        if (intervalMs < kKeyboardRepeatIntervalMsMin)
            return kKeyboardRepeatIntervalMsMin;
        if (intervalMs > kKeyboardRepeatIntervalMsMax)
            return kKeyboardRepeatIntervalMsMax;
        return intervalMs;
    }

    inline QC::u32 &keyboardRepeatDelayMsStorage()
    {
        static QC::u32 delayMs = kKeyboardRepeatDelayMsDefault;
        return delayMs;
    }

    inline QC::u32 &keyboardRepeatIntervalMsStorage()
    {
        static QC::u32 intervalMs = kKeyboardRepeatIntervalMsDefault;
        return intervalMs;
    }

    inline QC::u32 keyboardRepeatDelayMs()
    {
        return keyboardRepeatDelayMsStorage();
    }

    inline QC::u32 keyboardRepeatIntervalMs()
    {
        return keyboardRepeatIntervalMsStorage();
    }

    inline void setKeyboardRepeatDelayMs(QC::u32 delayMs)
    {
        keyboardRepeatDelayMsStorage() = clampKeyboardRepeatDelayMs(delayMs);
    }

    inline void setKeyboardRepeatIntervalMs(QC::u32 intervalMs)
    {
        keyboardRepeatIntervalMsStorage() = clampKeyboardRepeatIntervalMs(intervalMs);
    }

    inline void setKeyboardRepeatTiming(QC::u32 delayMs, QC::u32 intervalMs)
    {
        setKeyboardRepeatDelayMs(delayMs);
        setKeyboardRepeatIntervalMs(intervalMs);
    }
}