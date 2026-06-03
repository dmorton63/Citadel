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
}