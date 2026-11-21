#include <cassert>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "QCLogger.h"
#include "QFSFile.h"
#include "QKEntropy.h"
#include "QKSecureStore.h"

static std::map<std::string, std::vector<QC::u8>> files;
static int writes = 0;
static int seals = 0;
static bool unsealFails = false;

namespace QC
{
    Logger::Logger() : m_level(LogLevel::Info), m_mirror(nullptr) {}
    Logger &Logger::instance()
    {
        static Logger logger;
        return logger;
    }
    void Logger::error(const char *, const char *, ...) {}
}

namespace QK::Entropy
{
    QC::Status fillRandom(void *out, QC::usize size)
    {
        std::memset(out, 0x42, size);
        return QC::Status::Success;
    }
}

namespace QFS
{
    VFS::VFS() = default;
    VFS::~VFS() = default;
    VFS &VFS::instance()
    {
        static VFS vfs;
        return vfs;
    }
    QC::Status VFS::stat(const char *path, FileInfo *info)
    {
        if (std::strcmp(path, "/test") == 0)
        {
            info->type = FileType::Directory;
            return QC::Status::Success;
        }
        return QC::Status::NotFound;
    }
    QC::Status VFS::createDir(const char *) { ++writes; return QC::Status::Success; }
    QC::Status VFS::remove(const char *path)
    {
        ++writes;
        files.erase(path);
        return QC::Status::Success;
    }
    File::File() : m_open(false), m_mode(OpenMode::Read), m_position(0), m_size(0), m_fs(nullptr), m_fsHandle(nullptr) {}
    File::~File() = default;
    File *VFS::open(const char *path, OpenMode mode)
    {
        auto it = files.find(path);
        if (mode & OpenMode::Write)
        {
            ++writes;
            it = files.emplace(path, std::vector<QC::u8>{}).first;
            it->second.clear();
        }
        if (it == files.end())
            return nullptr;
        auto *file = new File();
        file->setHandle(&it->second);
        file->setSize(it->second.size());
        return file;
    }
    QC::Status VFS::close(File *file) { delete file; return QC::Status::Success; }
    QC::isize File::read(void *out, QC::usize size)
    {
        auto &data = *static_cast<std::vector<QC::u8> *>(handle());
        const QC::usize count = size < data.size() - m_position ? size : data.size() - m_position;
        std::memcpy(out, data.data() + m_position, count);
        m_position += count;
        return static_cast<QC::isize>(count);
    }
    QC::isize File::write(const void *in, QC::usize size)
    {
        auto &data = *static_cast<std::vector<QC::u8> *>(handle());
        const auto *bytes = static_cast<const QC::u8 *>(in);
        data.insert(data.end(), bytes, bytes + size);
        return static_cast<QC::isize>(size);
    }
    QC::Status File::sync() { return QC::Status::Success; }
}

int main()
{
    QK::SecureStore::Config cfg{};
    cfg.baseDir = "/test";
    cfg.tpmSealWrapKey = [](void *, const QC::u8 *key, QC::usize size, QC::Vector<QC::u8> &blob)
    {
        ++seals;
        blob.resize(size);
        std::memcpy(blob.data(), key, size);
        return QC::Status::Success;
    };
    cfg.tpmUnsealWrapKey = [](void *, const QC::Vector<QC::u8> &blob, QC::u8 *key, QC::usize size)
    {
        if (unsealFails || blob.size() != size)
            return QC::Status::Error;
        std::memcpy(key, blob.data(), size);
        return QC::Status::Success;
    };

    QC::Vector<QC::u8> result;
    const char *stage = nullptr;
    QK::SecureStore::WrapKeyDiagnostic diagnostic{};
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg, &diagnostic) == QC::Status::NotFound);
    assert(std::strcmp(stage, "read-sealed") == 0);
    assert(std::strcmp(diagnostic.path, "tpm") == 0);
    assert(std::strcmp(diagnostic.operation, "not-attempted") == 0);

    files["/test/OWNERCRD"] = std::vector<QC::u8>(40, 0);
    std::memcpy(files["/test/OWNERCRD"].data(), "SSB1", 4);
    files["/test/OWNERCRD"][4] = 1;
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg, &diagnostic) == QC::Status::NotFound);
    assert(std::strcmp(stage, "load-wrap-key") == 0);
    assert(std::strcmp(diagnostic.operation, "read-tpm-key") == 0);
    assert(files.size() == 1 && writes == 0 && seals == 0);

    const char content[] = "test credential record";
    assert(QK::SecureStore::writeSealedBlob("OWNERCRD", content, sizeof(content), cfg) == QC::Status::Success);
    const auto snapshot = files;
    const int writesBefore = writes;
    const int sealsBefore = seals;
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg) == QC::Status::Success);
    assert(std::strcmp(stage, "ok") == 0);
    assert(result.size() == sizeof(content) && std::memcmp(result.data(), content, sizeof(content)) == 0);
    assert(files == snapshot && writes == writesBefore && seals == sealsBefore);

    cfg.tpmDiagnoseUnsealWrapKey = [](void *, const QC::Vector<QC::u8> &, QC::u8 *, QC::usize,
                                       QK::SecureStore::WrapKeyDiagnostic &detail)
    {
        detail.operation = "unseal";
        detail.hasTpmResult = true;
        detail.tpmResult = 0x99d;
        return QC::Status::Error;
    };
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg, &diagnostic) == QC::Status::Error);
    assert(std::strcmp(stage, "load-wrap-key") == 0);
    assert(std::strcmp(diagnostic.operation, "unseal") == 0);
    assert(diagnostic.hasTpmResult && diagnostic.tpmResult == 0x99d);
    cfg.tpmDiagnoseUnsealWrapKey = nullptr;
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg, &diagnostic) == QC::Status::Success);
    assert(!diagnostic.hasTpmResult);
    unsealFails = true;
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg, &diagnostic) == QC::Status::Error);
    assert(std::strcmp(diagnostic.operation, "tpm-unseal") == 0 && !diagnostic.hasTpmResult);
    unsealFails = false;

    QK::SecureStore::Config recoveryCfg{};
    recoveryCfg.baseDir = "/test";
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, recoveryCfg, &diagnostic) == QC::Status::NotFound);
    assert(std::strcmp(diagnostic.path, "recovery") == 0);
    assert(std::strcmp(diagnostic.operation, "read-recovery-key") == 0);
    files["/test/WRAPKEY.KDF"] = std::vector<QC::u8>(88, 0);
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, recoveryCfg, &diagnostic) == QC::Status::Busy);
    assert(std::strcmp(diagnostic.operation, "recovery-locked") == 0);
    files.erase("/test/WRAPKEY.KDF");
    files["/test/OWNERCRD"][24] ^= 1;
    const auto tampered = files;
    assert(QK::SecureStore::readExistingSealedBlob("OWNERCRD", result, stage, cfg) == QC::Status::Error);
    assert(std::strcmp(stage, "sealed-tag") == 0);
    assert(files == tampered && writes == writesBefore && seals == sealsBefore);
}
