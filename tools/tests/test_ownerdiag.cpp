#include <cassert>
#include <cstring>

#include "QKSecureStore.h"
#include "QKSecurityCenter.h"

struct TestOwnerRecord
{
    QC::u32 magic = 0x4F435244;
    QC::u32 version = 2;
    QC::u8 userLen = 4;
    QC::u8 saltLen = 16;
    QC::u16 reserved = 0;
    QC::u32 iterations = 2000;
    QC::u8 salt[16] = {};
    QC::u8 verifier[32] = {};
    char username[32] = "test";
};

static QC::Status readStatus = QC::Status::Success;
static const char *readStage = "ok";
static TestOwnerRecord record;
static int readCount = 0;
static bool changeSecondRead = false;
static bool primaryMissing = false;
static bool primaryKeyMissing = false;

namespace QK::SecureStore
{
    Config defaultConfig() { return Config{.baseDir = "/system/.sc"}; }
    QC::Status readSealedBlob(const char *, QC::Vector<QC::u8> &, const Config &)
    {
        assert(false && "diagnostic must never use provisioning read");
        return QC::Status::Error;
    }
    QC::Status readExistingSealedBlob(const char *, QC::Vector<QC::u8> &out,
                                      const char *&stage, const Config &cfg)
    {
        ++readCount;
        if (primaryMissing && std::strcmp(cfg.baseDir, "/system") != 0)
        {
            stage = "read-sealed";
            return QC::Status::NotFound;
        }
        if (primaryKeyMissing && std::strcmp(cfg.baseDir, "/system") != 0)
        {
            stage = "load-wrap-key";
            return QC::Status::NotFound;
        }
        if (primaryKeyMissing && readStatus == QC::Status::NotFound)
        {
            stage = "read-sealed";
            return QC::Status::NotFound;
        }
        stage = readStage;
        if (readStatus != QC::Status::Success)
            return readStatus;
        TestOwnerRecord copy = record;
        if (changeSecondRead && readCount == 2)
            copy.verifier[0] = 1;
        out.resize(sizeof(copy));
        std::memcpy(out.data(), &copy, sizeof(copy));
        return QC::Status::Success;
    }
}

int main()
{
    auto &sc = QK::SecurityCenter::instance();
    const char *stage = nullptr;
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Success);
    assert(std::strcmp(stage, "ok") == 0 && readCount == 2);

    readCount = 0;
    readStatus = QC::Status::Error;
    readStage = "sealed-tag";
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Error);
    assert(std::strcmp(stage, "sealed-tag") == 0 && readCount == 1);

    readCount = 0;
    primaryKeyMissing = true;
    readStatus = QC::Status::NotFound;
    readStage = "load-wrap-key";
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::NotFound);
    assert(std::strcmp(stage, "load-wrap-key") == 0 && readCount == 2);

    readCount = 0;
    readStatus = QC::Status::Success;
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Success);
    assert(std::strcmp(stage, "ok") == 0 && readCount == 4);
    primaryKeyMissing = false;

    readCount = 0;
    readStage = "ok";
    primaryMissing = true;
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Success);
    assert(std::strcmp(stage, "ok") == 0 && readCount == 4);
    primaryMissing = false;

    readCount = 0;
    changeSecondRead = true;
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Error);
    assert(std::strcmp(stage, "credential-consistency") == 0 && readCount == 2);
    changeSecondRead = false;

    readCount = 0;
    record.saltLen = 0;
    assert(sc.diagnoseOwnerCredentials(stage) == QC::Status::Error);
    assert(std::strcmp(stage, "credential-format") == 0 && readCount == 1);
}
