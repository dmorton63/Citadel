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
        };

        // Returns the default secure store configuration.
        Config defaultConfig();

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

        // Reads and verifies a sealed blob from baseDir/<key>.
        QC::Status readSealedBlob(const char *key,
                                  QC::Vector<QC::u8> &out,
                                  const Config &cfg = defaultConfig());

        // Deletes baseDir/<key>.
        QC::Status removeBlob(const char *key, const Config &cfg = defaultConfig());

        // Returns true if baseDir/<key> exists.
        bool exists(const char *key, const Config &cfg = defaultConfig());

    } // namespace SecureStore

} // namespace QK
