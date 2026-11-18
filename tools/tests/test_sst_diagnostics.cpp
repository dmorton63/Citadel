#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "QSCSecurityCenter.h"
#include "QCLogger.h"

static char lastError[160];

namespace QC
{
    Logger::Logger() : m_level(LogLevel::Info), m_mirror(nullptr) {}
    Logger &Logger::instance()
    {
        static Logger logger;
        return logger;
    }
    void Logger::error(const char *, const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        std::vsnprintf(lastError, sizeof(lastError), format, args);
        va_end(args);
    }
}

int main()
{
    auto &sc = QSC::SecurityCenter::instance();
    std::vector<QC::u8> blob;
    QC::Status readStatus = QC::Status::NotFound;
    QC::Status rootStatus = QC::Status::Success;
    QSC::SstStorageProvider storage;
    struct ReadState
    {
        std::vector<QC::u8> *blob;
        QC::Status *status;
    } state{&blob, &readStatus};
    storage.user = &state;
    storage.writeWrappedSst = [](void *user, const void *data, QC::usize size)
    {
        auto &bytes = *static_cast<ReadState *>(user)->blob;
        const auto *begin = static_cast<const QC::u8 *>(data);
        bytes.assign(begin, begin + size);
        return QC::Status::Success;
    };
    storage.readWrappedSst = [](void *user, void *out, QC::usize capacity, QC::usize *size)
    {
        auto &s = *static_cast<ReadState *>(user);
        if (*s.status != QC::Status::Success)
            return *s.status;
        assert(s.blob->size() <= capacity);
        std::memcpy(out, s.blob->data(), s.blob->size());
        *size = s.blob->size();
        return QC::Status::Success;
    };
    sc.setSstStorageProvider(storage);
    QSC::RandomProvider random;
    random.fillRandom = [](void *, void *out, QC::usize size)
    {
        std::memset(out, 0x42, size);
        return QC::Status::Success;
    };
    sc.setRandomProvider(random);
    QSC::SrkProvider root;
    root.user = &rootStatus;
    root.getSrk = [](void *user, QSC::SrkKey &key)
    {
        std::memset(key.bytes, 0x19, sizeof(key.bytes));
        key.size = sizeof(key.bytes);
        return *static_cast<QC::Status *>(user);
    };
    sc.setSrkProvider(root);

    readStatus = QC::Status::Error;
    assert(sc.ensureSst() == QC::Status::Error);
    char expected[160];
    std::snprintf(expected, sizeof(expected), "stage=read-wrapped status=%d",
                  static_cast<QC::i32>(QC::Status::Error));
    assert(std::strcmp(lastError, expected) == 0);
    readStatus = QC::Status::NotFound;
    assert(sc.ensureSst() == QC::Status::Success);
    readStatus = QC::Status::Success;
    assert(sc.ensureSst() == QC::Status::Success);
    rootStatus = QC::Status::Error;
    assert(sc.ensureSst() == QC::Status::Error);
    assert(std::strstr(lastError, "stage=load-root-key status="));
    rootStatus = QC::Status::Success;
    blob.back() ^= 1;
    assert(sc.ensureSst() == QC::Status::Error);
    assert(std::strstr(lastError, "stage=verify-tag status="));
    std::cout << "SST failure-stage diagnostics passed\n";
}
