#pragma once

#include "QCTypes.h"

#include "QQFlowPolicy.h"

namespace QSC
{

    // System Security Token (SST) scaffolding.
    // Spec lives in LOG_DOC.md (Runtime Trust Model).
    struct SstStatus
    {
        bool available = false;
        QC::u64 generation = 0;
    };

    struct SstRotationRequest
    {
        bool forced = false;
        QC::u32 reasonCode = 0;
    };

    struct SstDerivedKey
    {
        QC::u8 bytes[32];
        QC::usize size = 32;
    };

    // Random provider boundary (kernel provides entropy source).
    struct RandomProvider
    {
        void *user = nullptr;
        QC::Status (*fillRandom)(void *user, void *out, QC::usize size) = nullptr;
    };

    // SST storage boundary (kernel provides protected persistence).
    // Stores the wrapped SST blob (ciphertext + metadata), never plaintext SST.
    struct SstStorageProvider
    {
        void *user = nullptr;
        QC::Status (*readWrappedSst)(void *user, void *out, QC::usize outCap, QC::usize *outSize) = nullptr;
        QC::Status (*writeWrappedSst)(void *user, const void *data, QC::usize size) = nullptr;
    };

    // Security Center Root Key (SRK) provider boundary.
    //
    // SRK is derived from the machine anchor (TAS) and is used only to wrap/unwrap
    // higher-level secrets like SST. QSC itself does not know how to obtain TAS/SRK;
    // the kernel wires an implementation via this callback.
    struct SrkKey
    {
        QC::u8 bytes[32];
        QC::usize size = 32;
    };

    struct SrkProvider
    {
        void *user = nullptr;
        QC::Status (*getSrk)(void *user, SrkKey &outKey) = nullptr;
    };

    enum class Mode : QC::u8
    {
        Bypass,
        Enforce
    };

    class SecurityCenter
    {
    public:
        static SecurityCenter &instance();

        void initialize(Mode mode);

        // Runtime flow governance toggle (MVP).
        void setFlowEnforcementEnabled(bool enabled);
        bool flowEnforcementEnabled() const { return m_mode == Mode::Enforce; }

        bool initialized() const { return m_initialized; }
        Mode mode() const { return m_mode; }

        // Flow policy used by QQ::Executor.
        QQ::FlowPolicyFn flowPolicy() const { return m_flowPolicy; }

        // SST: minimal public surface (stubs until provisioning/persistence is wired).
        SstStatus sstStatus() const;
        QC::Status ensureSst();
        QC::Status requestSstRotation(const SstRotationRequest &req);
        QC::Status deriveSstKey(const char *label, SstDerivedKey &outKey) const;

        // TAS/SRK boundary (kernel-provided).
        void setSrkProvider(const SrkProvider &provider);
        QC::Status getSrk(SrkKey &outKey) const;

        void setRandomProvider(const RandomProvider &provider);
        void setSstStorageProvider(const SstStorageProvider &provider);

    private:
        SecurityCenter();

        QC::Status loadOrProvisionSst(bool allowCreate);

        bool m_initialized;
        Mode m_mode;
        QQ::FlowPolicyFn m_flowPolicy;

        // SST state (scaffolding).
        bool m_sstAvailable = false;
        QC::u64 m_sstGeneration = 0;
        QC::u8 m_sstBytes[32] = {0};

        SrkProvider m_srkProvider{};
        RandomProvider m_randProvider{};
        SstStorageProvider m_sstStorage{};
    };

    const char *modeName(Mode mode);

} // namespace QSC
