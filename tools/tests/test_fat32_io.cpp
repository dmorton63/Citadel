#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "QCLogger.h"
#include "QFSFAT32.h"
#include "QFSFile.h"
#include "QKMemHeap.h"

namespace QC
{
    Logger::Logger() : m_level(LogLevel::Info), m_mirror(nullptr) {}
    Logger &Logger::instance()
    {
        static Logger logger;
        return logger;
    }
    void Logger::info(const char *, const char *, ...) {}
    void Logger::error(const char *, const char *, ...) {}
}

namespace QK::Memory
{
    Heap::Heap() = default;
    Heap::~Heap() = default;
    Heap &Heap::instance()
    {
        static Heap heap;
        return heap;
    }
    void *Heap::allocate(QC::usize size) { return std::malloc(size); }
    void Heap::free(void *p) { std::free(p); }
}

class Device final : public QFS::BlockDevice
{
public:
    std::vector<QC::u8> data = std::vector<QC::u8>(100 * 512);
    QC::u64 failRead = 100;
    QC::u64 failWrite = 100;
    unsigned writes = 0;

    Device()
    {
        QFS::FAT32BootSector boot{};
        boot.bytesPerSector = 512;
        boot.sectorsPerCluster = 1;
        boot.reservedSectors = 1;
        boot.fatCount = 1;
        boot.sectorsPerFat32 = 1;
        boot.totalSectors32 = 100;
        boot.rootCluster = 2;
        std::memcpy(data.data(), &boot, sizeof(boot));
        const QC::u32 end = 0x0fffffff;
        std::memcpy(data.data() + 512 + 2 * 4, &end, sizeof(end));
    }

    QC::usize sectorSize() const override { return 512; }
    QC::u64 sectorCount() const override { return 100; }
    QC::Status readSector(QC::u64 sector, void *out) override
    {
        if (sector >= sectorCount() || sector == failRead)
            return QC::Status::Error;
        std::memcpy(out, data.data() + sector * 512, 512);
        return QC::Status::Success;
    }
    QC::Status writeSector(QC::u64 sector, const void *in) override
    {
        if (sector >= sectorCount() || sector == failWrite)
            return QC::Status::Error;
        ++writes;
        std::memcpy(data.data() + sector * 512, in, 512);
        return QC::Status::Success;
    }
    QC::Status readSectors(QC::u64 sector, QC::usize count, void *out) override
    {
        auto *bytes = static_cast<QC::u8 *>(out);
        for (QC::usize i = 0; i < count; ++i)
            if (readSector(sector + i, bytes + i * 512) != QC::Status::Success)
                return QC::Status::Error;
        return QC::Status::Success;
    }
    QC::Status writeSectors(QC::u64 sector, QC::usize count, const void *in) override
    {
        const auto *bytes = static_cast<const QC::u8 *>(in);
        for (QC::usize i = 0; i < count; ++i)
            if (writeSector(sector + i, bytes + i * 512) != QC::Status::Success)
                return QC::Status::Error;
        return QC::Status::Success;
    }
};

int main()
{
    Device device;
    QFS::FAT32 fs(&device);
    assert(fs.mount() == QC::Status::Success);
    QFS::FileInfo info{};
    assert(fs.stat("/NEW.TPM", &info) == QC::Status::NotFound);
    device.failRead = 2;
    assert(fs.stat("/NEW.TPM", &info) == QC::Status::Error);
    assert(!fs.open("/NEW.TPM", QFS::OpenMode::Write | QFS::OpenMode::Create));
    assert(device.writes == 0);
    device.failRead = 100;

    auto *entries = reinterpret_cast<QFS::FAT32DirEntry *>(device.data.data() + 2 * 512);
    for (unsigned i = 0; i < 16; ++i)
    {
        std::memcpy(entries[i].name, "OTHER   TXT", 11);
        entries[i].attributes = QFS::FAT32Attr::Archive;
    }
    device.failRead = 1;
    assert(fs.stat("/NEW.TPM", &info) == QC::Status::Error);
    assert(!fs.open("/NEW.TPM", QFS::OpenMode::Write | QFS::OpenMode::Create));
    assert(device.writes == 0);
    device.failRead = 100;
    std::memset(entries, 0, 512);

    QFS::File *file = fs.open("/NEW.TPM", QFS::OpenMode::Write | QFS::OpenMode::Create);
    assert(file);
    device.failRead = 1;
    assert(file->write("x", 1) <= 0);
    assert(fs.close(file) == QC::Status::Success);
    delete file;
    device.failRead = 100;

    file = fs.open("/NEW.TPM", QFS::OpenMode::Write);
    assert(file);
    device.failWrite = 1;
    assert(file->write("x", 1) <= 0);
    assert(fs.close(file) == QC::Status::Success);
    delete file;
    device.failWrite = 100;

    file = fs.open("/NEW.TPM", QFS::OpenMode::Write);
    assert(file && file->write("x", 1) == 1);
    device.failWrite = 2;
    assert(fs.close(file) == QC::Status::Error);
    delete file;
    device.failWrite = 100;

    file = fs.open("/GOOD.TPM", QFS::OpenMode::Write | QFS::OpenMode::Create);
    assert(file && file->write("ok", 2) == 2);
    assert(fs.close(file) == QC::Status::Success);
    delete file;
    assert(fs.stat("/GOOD.TPM", &info) == QC::Status::Success && info.size == 2);
    file = fs.open("/GOOD.TPM", QFS::OpenMode::Read);
    char content[2];
    assert(file && file->read(content, sizeof(content)) == 2);
    assert(std::memcmp(content, "ok", 2) == 0);
    assert(fs.close(file) == QC::Status::Success);
    delete file;
}
