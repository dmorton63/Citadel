#include "QFSFileWatcher.h"
#include "QCString.h"

namespace QFS
{
    FileWatcher &FileWatcher::instance()
    {
        static FileWatcher watcher;
        return watcher;
    }

    FileWatcher::WatchRecord *FileWatcher::findRecord(const char *path, FileWatchCallback callback, void *userData)
    {
        if (!path || !*path)
            return nullptr;

        for (QC::usize i = 0; i < m_records.size(); ++i)
        {
            WatchRecord &record = m_records[i];
            if (QC::String::strcmp(record.path, path) != 0)
                continue;
            if (callback && record.callback != callback)
                continue;
            if (userData && record.userData != userData)
                continue;
            return &record;
        }

        return nullptr;
    }

    QC::Status FileWatcher::subscribe(const char *path, FileWatchCallback callback, void *userData)
    {
        if (!path || !*path || !callback)
            return QC::Status::InvalidParam;

        if (findRecord(path, callback, userData))
            return QC::Status::Success;

        WatchRecord record{};
        QC::String::strncpy(record.path, path, sizeof(record.path) - 1);
        record.path[sizeof(record.path) - 1] = '\0';
        record.callback = callback;
        record.userData = userData;

        FileInfo info{};
        if (statPath(path, &info) == QC::Status::Success)
        {
            record.knownExists = true;
            record.lastModifiedTime = info.modifiedTime;
        }

        m_records.push_back(record);
        return QC::Status::Success;
    }

    QC::Status FileWatcher::unsubscribe(const char *path, FileWatchCallback callback, void *userData)
    {
        if (!path || !*path)
            return QC::Status::InvalidParam;

        for (QC::usize i = 0; i < m_records.size(); ++i)
        {
            WatchRecord &record = m_records[i];
            if (QC::String::strcmp(record.path, path) != 0)
                continue;
            if (callback && record.callback != callback)
                continue;
            if (userData && record.userData != userData)
                continue;

            if (i + 1 < m_records.size())
                m_records[i] = m_records[m_records.size() - 1];
            m_records.pop_back();
            return QC::Status::Success;
        }

        return QC::Status::NotFound;
    }

    void FileWatcher::tick()
    {
        for (QC::usize i = 0; i < m_records.size(); ++i)
        {
            WatchRecord &record = m_records[i];
            FileInfo info{};
            const QC::Status st = statPath(record.path, &info);

            const bool exists = (st == QC::Status::Success);
            const QC::u64 modifiedTime = exists ? info.modifiedTime : 0;
            const bool changed = (!record.knownExists && exists) ||
                                 (record.knownExists && !exists) ||
                                 (record.knownExists && exists && record.lastModifiedTime != modifiedTime);

            if (!changed)
                continue;

            record.knownExists = exists;
            record.lastModifiedTime = modifiedTime;

            if (record.callback)
            {
                FileWatchEvent event{};
                event.path = record.path;
                event.exists = exists;
                event.modifiedTime = modifiedTime;
                record.callback(event, record.userData);
            }
        }
    }

    void FileWatcher::clear()
    {
        m_records.clear();
    }
}