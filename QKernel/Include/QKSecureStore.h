#pragma once

#include "QCTypes.h"
#include "QCVector.h"

namespace QK
{

    namespace SecureStore
    {

        // Minimal persistence layer for security-sensitive subsystems.
        //
        // Notes:
        // - This is *not* encryption yet; it is only a controlled location + API surface.
        // - Key names are intentionally constrained to FAT 8.3 to avoid collisions/truncation.

        struct Config
        {
            const char *baseDir; // default: "/system/sc"

            // Optional TPM-backed wrap-key support.
            //
            // If both callbacks are provided, sealed blobs will use a wrap key
            // that is sealed/unsealed by the TPM. The sealed object blob is
            // still stored in the SecureStore directory, but the raw wrap key
            // is never persisted in plaintext.
            void *tpmUser = nullptr;

            // Produce a TPM-sealed blob for the wrap key.
            // outBlob must be filled with an opaque blob that can later be
            // passed to tpmUnsealWrapKey to recover the key.
            QC::Status (*tpmSealWrapKey)(void *user,
                                         const QC::u8 *wrapKey,
                                         QC::usize wrapKeyLen,
                                         QC::Vector<QC::u8> &outBlob) = nullptr;

            // Recover the wrap key from a previously produced blob.
            QC::Status (*tpmUnsealWrapKey)(void *user,
                                           const QC::Vector<QC::u8> &blob,
                                           QC::u8 *outWrapKey,
                                           QC::usize outWrapKeyLen) = nullptr;

            // Optional TPM-backed generic secret sealing.
            //
            // These APIs intentionally operate on opaque blobs, so callers can
            // store them wherever appropriate. The TPM policy binding (e.g., PCRs)
            // is owned by the implementation behind these callbacks.
            QC::Status (*tpmSealSecret)(void *user,
                                        const QC::u8 *secret,
                                        QC::usize secretLen,
                                        QC::Vector<QC::u8> &outBlob) = nullptr;

            QC::Status (*tpmUnsealSecret)(void *user,
                                          const QC::Vector<QC::u8> &blob,
                                          QC::u8 *outSecret,
                                          QC::usize outSecretCap,
                                          QC::usize *outSecretLen) = nullptr;
        };

        // Returns the default secure store configuration.
        Config defaultConfig();

        // Returns true if TPM-backed wrap-key sealing is enabled in the config.
        // This is the minimal "TPM present" probe for subsystems that only care
        // whether sealing/unsealing is available (not the transport details).
        bool tpm_present(const Config &cfg = defaultConfig());

        // Overrides the process-wide default secure store configuration.
        // This is intended for early-boot initialization (e.g., enabling TPM
        // wrap-key sealing). Callers that pass an explicit Config are
        // unaffected.
        void setDefaultConfig(const Config &cfg);

        // Ensures baseDir exists (creates missing directories).
        QC::Status ensureBaseDir(const Config &cfg = defaultConfig());

        // Writes a blob to baseDir/<key>. Overwrites existing content.
        QC::Status writeBlob(const char *key,
                             const void *data,
                             QC::usize size,
                             const Config &cfg = defaultConfig());

        // Reads a blob from baseDir/<key>.
        QC::Status readBlob(const char *key,
                            QC::Vector<QC::u8> &out,
                            const Config &cfg = defaultConfig());

        // Writes a sealed blob to baseDir/<key>.
        //
        // Sealed blobs are stored as an authenticated-encrypted payload.
        // The current implementation uses a software wrap key persisted under
        // the secure store base directory.
        QC::Status writeSealedBlob(const char *key,
                                   const void *data,
                                   QC::usize size,
                                   const Config &cfg = defaultConfig());

        // Writes a TPM-accelerated sealed blob to baseDir/<key>.
        //
        // If a TPM secret sealing provider is available (Config::tpmSealSecret /
        // Config::tpmUnsealSecret), this seals a fresh per-blob content key via
        // TPM policy and then encrypts/authenticates the payload with that key.
        //
        // If no TPM secret sealing provider is present, falls back to
        // writeSealedBlob().
        QC::Status writeTpmSealedBlob(const char *key,
                                      const void *data,
                                      QC::usize size,
                                      const Config &cfg = defaultConfig());

        // Reads and verifies a sealed blob from baseDir/<key>.
        QC::Status readSealedBlob(const char *key,
                                  QC::Vector<QC::u8> &out,
                                  const Config &cfg = defaultConfig());

        // Reads a blob written by writeTpmSealedBlob().
        //
        // If the stored blob is TPM-accelerated format, TPM secret unseal must
        // be available or this returns NotSupported.
        // If the stored blob is the legacy sealed format, it is read via
        // readSealedBlob().
        QC::Status readTpmSealedBlob(const char *key,
                                     QC::Vector<QC::u8> &out,
                                     const Config &cfg = defaultConfig());

        // Reads the current SecureStore wrap key without creating a new one.
        //
        // Returns:
        // - Success: outWrapKey filled.
        // - NotFound: wrap key not present yet.
        // - Error: wrap key present but invalid/unsealable.
        QC::Status readWrapKey(QC::u8 outWrapKey[32], const Config &cfg = defaultConfig());

        // Returns the SecureStore wrap key, creating it if necessary.
        //
        // This is intended for provisioning/first-run flows.
        QC::Status getOrCreateWrapKey(QC::u8 outWrapKey[32], const Config &cfg = defaultConfig());

        // --- TPM Anchor Secret (TAS) ---
        //
        // TAS is the machine-bound anchor secret used to derive higher-level
        // keys (e.g., Security Center Root Key).
        //
        // Current implementation:
        // - TPM present: TAS is the SecureStore wrap key sealed via TPM policy
        //   and stored as a sealed blob (WRAPKEY.TPM).
        // - No TPM: TAS is the SecureStore wrap key wrapped under the recovery
        //   derived key (WRAPKEY.KDF).
        //
        // This keeps the system anchored to a single persistent secret today;
        // the anchor can be split into dedicated TAS vs storage-wrap keys later.
        QC::Status readTas(QC::u8 outTas[32], const Config &cfg = defaultConfig());
        QC::Status getOrCreateTas(QC::u8 outTas[32], const Config &cfg = defaultConfig());

        // Deletes baseDir/<key>.
        QC::Status removeBlob(const char *key, const Config &cfg = defaultConfig());

        // Returns true if baseDir/<key> exists.
        bool exists(const char *key, const Config &cfg = defaultConfig());

        // --- Non-TPM secure bootstrapping (recovery code -> KDF -> wraps anchor secret) ---
        //
        // When TPM sealing is unavailable, SecureStore can protect its wrap key at-rest by
        // wrapping it under a key derived from a user-provided recovery code.
        //
        // This function must be called (early in boot) before any subsystem attempts to
        // read or create the wrap key.
        QC::Status nonTpmUnlockOrInitializeWrapKey(const char *recoveryCode, const Config &cfg = defaultConfig());

        // --- TPM secret sealing abstraction (TPM-backed; stubbed fallback) ---
        //
        // When TPM sealing is available (callbacks provided), secrets are sealed
        // and can be unsealed only when the policy holds. Without TPM support,
        // these return NotSupported.
        QC::Status seal_secret(const void *secret,
                               QC::usize secretLen,
                               QC::Vector<QC::u8> &outSealedBlob,
                               const Config &cfg = defaultConfig());

        QC::Status unseal_secret(const QC::Vector<QC::u8> &sealedBlob,
                                 QC::u8 *outSecret,
                                 QC::usize outSecretCap,
                                 QC::usize *outSecretLen,
                                 const Config &cfg = defaultConfig());

    } // namespace SecureStore

} // namespace QK
