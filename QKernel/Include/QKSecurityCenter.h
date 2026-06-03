#pragma once

#include "QCTypes.h"

namespace QK
{

    class SecurityCenter
    {
    public:
        enum class ProvisioningState : QC::u8
        {
            Unprovisioned = 0,
            Provisioned,
            Operational,
            Recovery,
            SafeMode
        };

        enum class DispatchOp : QC::u8
        {
            TrustCheck = 0,
            UpdateVerify,
            RotateSst,
            ExecRequest,
            VaultRequest,
            AuditView,
            AuditExport,
        };

        struct DispatchRequest
        {
            DispatchOp op = DispatchOp::TrustCheck;
            QC::u32 flags = 0;
            const char *payload = nullptr;
        };

        struct DispatchResult
        {
            QC::Status status = QC::Status::NotSupported;
            char detail[96] = {0};
        };

        struct CorruptionPolicyDecision
        {
            bool allowOperation = false;
            bool requireRecovery = true;
            bool markCompromised = true;
            bool enterSafeMode = true;
            char detail[96] = {0};
        };

        struct RotationFailurePolicyDecision
        {
            bool rollbackToPreviousGeneration = true;
            bool markDegraded = true;
            bool enterSafeMode = false;
            bool continueBoot = true;
            char detail[96] = {0};
        };

        struct TasFailurePolicyDecision
        {
            bool enterSafeMode = true;
            bool enterRecovery = true;
            bool allowNormalOperation = false;
            char detail[96] = {0};
        };

        struct KeyHierarchySnapshot
        {
            bool valid = false;
            QC::u8 srk[32] = {0};
            QC::u8 sstMix[32] = {0};
            QC::u8 root[32] = {0};
            QC::u8 vault[32] = {0};
        };

        struct OwnerVaultHeader
        {
            bool valid = false;
            char username[32] = {0};
            QC::u8 salt[16] = {0};
            QC::u32 iterations = 0;
            QC::u8 wrappedVrk[32] = {0};
        };

        struct PayloadScanResult
        {
            bool allowed = false;
            bool suspicious = false;
            bool quarantined = false;
            char detail[96] = {0};
        };

        struct RotationScheduleConfig
        {
            bool enabled = true;
            QC::u8 reserved[7] = {0};
            QC::u64 minIntervalMs = 30ULL * 60ULL * 1000ULL;
            QC::u64 minExecutedTasks = 64;
            QC::u64 boundaryTimeoutMs = 250;
        };

        enum class NetworkPortProtocol : QC::u8
        {
            Unknown = 0,
            UDP = 1,
            TCP = 2,
        };

        struct NetworkCapabilityToken
        {
            QC::u8 id[16] = {0};
            char type[24] = {0};
            char scope[16] = {0};
            QC::u32 issuedToPid = 0;
            bool hasExpiration = false;
            QC::u8 reserved[3] = {0};
            QC::u64 expiresAtMs = 0;
        };

        enum class UnlockState : QC::u8
        {
            Locked = 0,
            Unlocked,
            TimedLock
        };

        enum class Mode : QC::u8
        {
            Bypass,
            Enforce
        };

        static SecurityCenter &instance();

        void initialize(Mode mode);

        // Provision/load SST after persistent /system mount.
        QC::Status ensureSst();

        // v1 boot trust gate: requires TAS/SRK-backed SST load/provision success
        // and a valid Security Center runtime integrity state.
        QC::Status checkBootTrustGate();

        // Runtime flow governance toggle (MVP).
        void setFlowEnforcementEnabled(bool enabled);
        bool flowEnforcementEnabled() const { return m_mode == Mode::Enforce; }

        // Kernel-side SC dispatch bridge for SYS_* style operations.
        QC::Status dispatch(const DispatchRequest &req, DispatchResult *outResult = nullptr);

        // Port Manager Phase 1A scaffolding:
        // validates a Network.OpenPort token and its scope against requested protocol/port.
        bool parseNetworkOpenPortScope(const char *scope,
                           NetworkPortProtocol &outProtocol,
                           QC::u16 &outPort) const;
        bool validateNetworkOpenPortToken(const NetworkCapabilityToken &token,
                          NetworkPortProtocol requestedProtocol,
                          QC::u16 requestedPort,
                          QC::u32 requesterPid,
                          QC::u64 nowMs = 0) const;

        bool initialized() const { return m_initialized; }
        Mode mode() const { return m_mode; }
        bool bypassEnabled() const { return m_mode == Mode::Bypass; }

        // Owner credentials (minimal v1, single-owner).
        // Persisted via SecureStore sealed blobs; no DB.
        QC::Status ownerEnroll(const char *username, const char *secret, bool activateSession = true);
        QC::Status ownerUnlock(const char *username, const char *secret, bool activateSession = true);
        QC::Status ownerUnlockPasskey(const char *username, const char *passkey);
        QC::Status getEnrolledOwnerUsername(char *outUsername, QC::usize outCap) const;
        void debugDescribeOwnerRecord(char *outSummary, QC::usize outCap) const;
        void ownerLock();
        bool ownerIsEnrolled() const;
        bool ownerUnlocked() const { return m_ownerUnlocked; }
        bool ownerLockedOut() const;
        QC::u64 ownerLockoutRemainingMs() const;
        QC::u32 ownerUnlockBackoffMs() const { return m_ownerBackoffMs; }
        QC::u32 ownerUnlockAttemptCount() const { return m_ownerUnlockAttempts; }
        QC::u32 ownerUnlockFailureCount() const { return m_ownerUnlockFailures; }
        bool ownerUmkReady() const { return m_ownerUmkReady; }
        bool ownerVrkReady() const { return m_ownerVrkReady; }
        UnlockState unlockState() const { return m_unlockState; }
        void setTimedLock(bool enabled);

        // Key-schedule scaffold: derive a role/tier key from session UMK+VRK+SST.
        QC::Status deriveRoleTierKey(QC::u32 roleId, QC::u32 version, QC::u8 outKey[32]) const;

        // Rotation scaffold: rewrap tier key material from one version to another
        // without touching encrypted file contents.
        QC::Status rewrapTierKeyMaterial(QC::u32 roleId,
                         QC::u32 fromVersion,
                         QC::u32 toVersion,
                         const QC::u8 wrappedTierKey[32],
                         QC::u8 outWrappedTierKey[32]) const;

        CorruptionPolicyDecision decideVaultCorruptionPolicy(bool headerCorrupt, bool contentCorrupt) const;
        CorruptionPolicyDecision decideAuditChainCorruptionPolicy(bool chainInvalid) const;
        RotationFailurePolicyDecision decideSstRotationMidCutoverFailurePolicy(bool persistedNewWrappedSst,
                                               bool switchedGeneration,
                                               bool retiredOldSst) const;
        TasFailurePolicyDecision decideTasUnsealFailurePolicy() const;

        bool exportPolicyAllows(const char *channel) const;

        QC::Status waitForRotationBoundary(QC::u64 timeoutMs, QC::u32 *outActiveTasks = nullptr) const;
        bool hasUserFacingIdentity() const { return false; }
        bool startsAsBackgroundSystemTask() const { return true; }
        void auditDecision(DispatchOp op, bool approved, QC::Status st) const;
        QC::Status allowAuditLogAccess(bool exportRequest);
        void redactAuditText(const char *input, char *output, QC::usize cap) const;
        QC::Status secureReadFile(const char *path, void *outData, QC::usize cap, QC::usize *outRead) const;
        QC::Status secureWriteFile(const char *path, const void *data, QC::usize size, bool append) const;
        QC::Status generateInstallRecoveryCode(char outCode[48]);
        QC::Status consumePendingInstallRecoveryCode(char outCode[48]);
        QC::Status generatePerUserVaultHeader(const char *username, OwnerVaultHeader &outHeader) const;
        QC::Status scanPayload(const char *label, const void *data, QC::usize size, PayloadScanResult &out) const;
        QC::Status quarantinePayload(const char *artifactId, const void *data, QC::usize size, char outPath[192]) const;
        QC::Status parkVerifiedUpdate(const char *artifactId, const void *data, QC::usize size, char outPath[192]) const;
        void setRotationScheduleConfig(const RotationScheduleConfig &config);
        RotationScheduleConfig rotationScheduleConfig() const { return m_rotationSchedule; }
        bool shouldForceSstRotation() const;
        QC::Status maybeForceRotateSst(QC::u64 boundaryTimeoutMs = 250);
        bool protectedStorageInitialized() const { return m_protectedStorageInitialized; }
        bool sstRetiring() const { return m_sstRetiring; }

        QC::Status deriveInitialKeyHierarchyFromTas(KeyHierarchySnapshot &out) const;
        QC::Status deriveRecoveryKeyMemoryHard(const char *recoveryCode,
                               const QC::u8 salt[16],
                               QC::u32 iterations,
                               QC::u8 outKey[32]) const;
        void emitProvisioningCompletedAuditEvent(QC::u64 code = 0x50525631ULL) const;

        bool singleOwnerScope() const { return true; }
        ProvisioningState provisioningState() const { return m_provisioningState; }

        static const char *modeName(Mode mode);
        static const char *provisioningStateName(ProvisioningState state);

    private:
        SecurityCenter();
        void clearOwnerSessionKeys();
        QC::Status handleTrustCheck(DispatchResult *outResult);
        QC::Status handleUpdateVerify(const DispatchRequest &req, DispatchResult *outResult);
        QC::Status handleRotateSst(const DispatchRequest &req, DispatchResult *outResult);
        QC::Status handleExecRequest(const DispatchRequest &req, DispatchResult *outResult);
        QC::Status handleVaultRequest(const DispatchRequest &req, DispatchResult *outResult);
        QC::Status handleAuditView(DispatchResult *outResult);
        QC::Status handleAuditExport(DispatchResult *outResult);
        void refreshOperationalState();

        bool m_initialized;
        Mode m_mode;

        // Owner auth session state.
        bool m_ownerUnlocked = false;
        QC::u32 m_ownerFailCount = 0;
        QC::u32 m_ownerBackoffMs = 0;
        QC::u32 m_ownerUnlockAttempts = 0;
        QC::u32 m_ownerUnlockFailures = 0;
        QC::u64 m_ownerLockoutUntilMs = 0;
        QC::u32 m_ownerLockoutThreshold = 5;
        QC::u64 m_ownerLockoutDurationMs = 5ULL * 60ULL * 1000ULL;
        bool m_ownerUmkReady = false;
        bool m_ownerVrkReady = false;
        UnlockState m_unlockState = UnlockState::Locked;
        QC::u8 m_ownerUmk[32] = {0};
        QC::u8 m_ownerVrk[32] = {0};
        char m_pendingRecoveryCode[48] = {0};
        bool m_deferInstallRecoveryCode = false;
        RotationScheduleConfig m_rotationSchedule{};
        QC::u64 m_lastRotationMs = 0;
        QC::u64 m_lastRotationExecCount = 0;
        QC::u64 m_auditViewWindowStartMs = 0;
        QC::u32 m_auditViewWindowCount = 0;
        QC::u64 m_auditExportWindowStartMs = 0;
        QC::u32 m_auditExportWindowCount = 0;
        bool m_protectedStorageInitialized = false;
        bool m_sstRetiring = false;
        ProvisioningState m_provisioningState = ProvisioningState::Unprovisioned;
    };

} // namespace QK
