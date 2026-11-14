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
    // Command completion can take many seconds on discrete TPMs (RSA key generation).
    constexpr std::size_t kTpmCommandCompleteSpinIterations = 30'000'000u;
    constexpr std::uint32_t kTpmWarmupAttempts = 3u;
    constexpr std::uint32_t kTpmWarmupDelayMsBase = 5u;

    // ACPI TPM2 table start methods.
    constexpr std::uint32_t kTpmStartMethodAcpi = 2u;
    constexpr std::uint32_t kTpmStartMethodMmio = 6u;
    constexpr std::uint32_t kTpmStartMethodCrb = 7u;
    constexpr std::uint32_t kTpmStartMethodCrbAcpi = 8u;

    // TPM_INTERFACE_ID.InterfaceType values.
    constexpr std::uint32_t kTpmInterfaceTypeFifo = 0x0u;
    constexpr std::uint32_t kTpmInterfaceTypeCrb = 0x1u;
    constexpr std::uint32_t kTpmInterfaceTypeLegacyTis = 0xFu;

    enum class TransportKind : std::uint8_t
    {
        None = 0,
        Crb = 1,
        Tis = 2,
    };

    // Chooses the TPM transport. Start method 6 covers both TIS/FIFO and CRB, so
    // the interface ID decides; without one, a control area keeps the legacy CRB path.
    inline TransportKind ClassifyTransport(std::uint32_t startMethod, bool controlAreaPresent, bool interfaceIdValid, std::uint32_t interfaceType)
    {
        switch (startMethod)
        {
        case kTpmStartMethodMmio:
            if (interfaceIdValid)
            {
                if (interfaceType == kTpmInterfaceTypeCrb)
                    return controlAreaPresent ? TransportKind::Crb : TransportKind::None;
                if (interfaceType == kTpmInterfaceTypeFifo || interfaceType == kTpmInterfaceTypeLegacyTis)
                    return TransportKind::Tis;
                return TransportKind::None;
            }
            return controlAreaPresent ? TransportKind::Crb : TransportKind::Tis;
        case kTpmStartMethodAcpi:
        case kTpmStartMethodCrb:
        case kTpmStartMethodCrbAcpi:
            return controlAreaPresent ? TransportKind::Crb : TransportKind::None;
        default:
            return TransportKind::None;
        }
    }

    inline bool StartMethodRequestsAcpiStart(std::uint32_t startMethod)
    {
        return startMethod == kTpmStartMethodAcpi || startMethod == kTpmStartMethodCrbAcpi;
    }

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
