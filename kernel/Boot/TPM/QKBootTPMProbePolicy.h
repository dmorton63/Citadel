#pragma once

#include <cstddef>
#include <cstdint>

namespace QK::Boot::Tpm
{
    constexpr std::uint32_t kTpmStartupAlreadyInitialized = 0x00000100u;
    constexpr std::uint32_t kTpmTransportFailure = 0xFFFFFFFFu;
    constexpr std::uint32_t kTpmRetryableYielded = 0x00000908u;
    constexpr std::uint32_t kTpmRetryableRetry = 0x00000922u;
    constexpr std::uint32_t kTpmRetryableTesting = 0x00000990u;

    constexpr std::size_t kTpmCmdReadySpinIterations = 1'000'000u;
    constexpr std::size_t kTpmStartSpinIterations = 2'000'000u;
    constexpr std::uint32_t kTpmWarmupAttempts = 3u;
    constexpr std::uint32_t kTpmWarmupDelayMsBase = 5u;

    inline bool IsStartupResponseSuccessful(std::uint32_t response)
    {
        return response == 0u || response == kTpmStartupAlreadyInitialized;
    }

    inline bool IsTransportFailure(std::uint32_t response)
    {
        return response == kTpmTransportFailure;
    }

    inline bool IsRetryableTpmResponse(std::uint32_t response)
    {
        return IsTransportFailure(response) || response == kTpmRetryableYielded || response == kTpmRetryableRetry || response == kTpmRetryableTesting;
    }
}
