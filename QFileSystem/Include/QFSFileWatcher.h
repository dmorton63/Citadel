#pragma once

#include "QCTypes.h"
#include "QFSStat.h"
#include "QCVector.h"

namespace QFS
{
    struct FileWatchEvent
    {
        const char *path = nullptr;
        bool exists = false;
        QC::u64 modifiedTime = 0;
    };

    using FileWatchCallback = void (*)(const FileWatchEvent &event, void *userData);

    class FileWatcher
    {
    public:
        static FileWatcher &instance();

        QC::Status subscribe(const char *path, FileWatchCallback callback, void *userData = nullptr);
        QC::Status unsubscribe(const char *path, FileWatchCallback callback = nullptr, void *userData = nullptr);
        void tick();
        void clear();

    private:
        struct WatchRecord
        {
            char path[256] = {0};
            FileWatchCallback callback = nullptr;
            void *userData = nullptr;
            bool knownExists = false;
            QC::u64 lastModifiedTime = 0;
        };

        FileWatcher() = default;
        FileWatcher(const FileWatcher &) = delete;
        FileWatcher &operator=(const FileWatcher &) = delete;

        WatchRecord *findRecord(const char *path, FileWatchCallback callback, void *userData);

        QC::Vector<WatchRecord> m_records;
    };
}