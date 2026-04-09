#include "QKSimpleDb.h"

#include "QFSFile.h"
#include "QFSVFS.h"
#include "QCString.h"

namespace QK::Db
{
    namespace
    {
        static bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        static void trimInPlace(char *s)
        {
            if (!s)
                return;
            QC::usize len = QC::String::strlen(s);
            QC::usize start = 0;
            while (start < len && isSpace(s[start]))
                ++start;
            QC::usize end = len;
            while (end > start && isSpace(s[end - 1]))
                --end;
            QC::usize out = 0;
            for (QC::usize i = start; i < end; ++i)
                s[out++] = s[i];
            s[out] = '\0';
        }

        static bool readAll(const char *path, QC::Vector<char> &out)
        {
            out.clear();
            QFS::File *f = QFS::VFS::instance().open(path, QFS::OpenMode::Read);
            if (!f)
                return false;

            const QC::isize szSigned = f->size();
            if (szSigned < 0)
            {
                QFS::VFS::instance().close(f);
                return false;
            }

            const QC::usize sz = static_cast<QC::usize>(szSigned);
            out.resize(sz + 1);
            QC::usize off = 0;
            while (off < sz)
            {
                const QC::isize n = f->read(out.data() + off, sz - off);
                if (n <= 0)
                    break;
                off += static_cast<QC::usize>(n);
            }
            QFS::VFS::instance().close(f);

            if (off != sz)
            {
                out.clear();
                return false;
            }

            out[sz] = '\0';
            return true;
        }

        static bool writeAll(const char *path, const char *data, QC::usize size)
        {
            QFS::File *f = QFS::VFS::instance().open(path,
                                                     QFS::OpenMode::Write | QFS::OpenMode::Create | QFS::OpenMode::Truncate);
            if (!f)
                return false;

            QC::usize off = 0;
            while (off < size)
            {
                const QC::isize n = f->write(data + off, size - off);
                if (n <= 0)
                {
                    QFS::VFS::instance().close(f);
                    return false;
                }
                off += static_cast<QC::usize>(n);
            }

            QFS::VFS::instance().close(f);
            return true;
        }
    }

    Store &Store::instance()
    {
        static Store s;
        return s;
    }

    QC::Status Store::ensureLoaded() const
    {
        if (!m_loaded)
            return const_cast<Store *>(this)->load();
        return QC::Status::Success;
    }

    int Store::findKey(const char *key) const
    {
        if (!key || !*key)
            return -1;

        for (QC::usize i = 0; i < m_items.size(); ++i)
        {
            if (QC::String::strcmp(m_items[i].key, key) == 0)
                return static_cast<int>(i);
        }
        return -1;
    }

    QC::Status Store::load(const char *path)
    {
        if (!path || !*path)
            return QC::Status::InvalidParam;

        QC::String::memset(m_path, 0, sizeof(m_path));
        QC::String::strncpy(m_path, path, sizeof(m_path) - 1);
        m_items.clear();

        QC::Vector<char> content;
        if (!readAll(m_path, content))
        {
            m_loaded = true;
            return QC::Status::Success;
        }

        char line[320];
        QC::String::memset(line, 0, sizeof(line));
        QC::usize li = 0;

        auto parseLine = [&](char *raw)
        {
            trimInPlace(raw);
            if (raw[0] == '\0' || raw[0] == '#')
                return;

            const char *sep = nullptr;
            for (const char *p = raw; *p; ++p)
            {
                if (*p == '=')
                {
                    sep = p;
                    break;
                }
            }
            if (!sep)
                return;

            const QC::usize keyLen = static_cast<QC::usize>(sep - raw);
            const char *value = sep + 1;

            Item it;
            QC::String::memset(&it, 0, sizeof(it));
            if (keyLen > 0)
                QC::String::strncpy(it.key, raw, (keyLen < sizeof(it.key) - 1) ? keyLen : sizeof(it.key) - 1);
            QC::String::strncpy(it.value, value, sizeof(it.value) - 1);
            trimInPlace(it.key);
            trimInPlace(it.value);
            if (it.key[0] == '\0')
                return;

            const int index = findKey(it.key);
            if (index >= 0)
                m_items[static_cast<QC::usize>(index)] = it;
            else
                m_items.push_back(it);
        };

        const char *p = content.data();
        while (p && *p)
        {
            char c = *p++;
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                line[li] = '\0';
                parseLine(line);
                li = 0;
                line[0] = '\0';
                continue;
            }
            if (li + 1 < sizeof(line))
                line[li++] = c;
        }
        if (li)
        {
            line[li] = '\0';
            parseLine(line);
        }

        m_loaded = true;
        return QC::Status::Success;
    }

    QC::Status Store::save()
    {
        if (ensureLoaded() != QC::Status::Success)
            return QC::Status::Error;

        char out[8192];
        QC::String::memset(out, 0, sizeof(out));
        QC::usize used = 0;

        for (QC::usize i = 0; i < m_items.size(); ++i)
        {
            const Item &it = m_items[i];
            const QC::usize kLen = QC::String::strlen(it.key);
            const QC::usize vLen = QC::String::strlen(it.value);
            const QC::usize need = kLen + 1 + vLen + 1;
            if (used + need >= sizeof(out))
                return QC::Status::OutOfMemory;

            QC::String::strncpy(out + used, it.key, sizeof(out) - used - 1);
            used += kLen;
            out[used++] = '=';
            QC::String::strncpy(out + used, it.value, sizeof(out) - used - 1);
            used += vLen;
            out[used++] = '\n';
            out[used] = '\0';
        }

        if (!writeAll(m_path, out, used))
            return QC::Status::Error;
        return QC::Status::Success;
    }

    QC::Status Store::set(const char *key, const char *value)
    {
        if (!key || !*key || !value)
            return QC::Status::InvalidParam;
        if (ensureLoaded() != QC::Status::Success)
            return QC::Status::Error;

        const int index = findKey(key);
        if (index >= 0)
        {
            Item &it = m_items[static_cast<QC::usize>(index)];
            QC::String::memset(it.value, 0, sizeof(it.value));
            QC::String::strncpy(it.value, value, sizeof(it.value) - 1);
            return QC::Status::Success;
        }

        Item it;
        QC::String::memset(&it, 0, sizeof(it));
        QC::String::strncpy(it.key, key, sizeof(it.key) - 1);
        QC::String::strncpy(it.value, value, sizeof(it.value) - 1);
        m_items.push_back(it);
        return QC::Status::Success;
    }

    QC::Status Store::get(const char *key, char *outValue, QC::usize outCap) const
    {
        if (!key || !*key || !outValue || outCap == 0)
            return QC::Status::InvalidParam;
        if (ensureLoaded() != QC::Status::Success)
            return QC::Status::Error;

        const int index = findKey(key);
        if (index < 0)
            return QC::Status::NotFound;

        QC::String::memset(outValue, 0, outCap);
        QC::String::strncpy(outValue, m_items[static_cast<QC::usize>(index)].value, outCap - 1);
        return QC::Status::Success;
    }

    QC::Status Store::erase(const char *key)
    {
        if (!key || !*key)
            return QC::Status::InvalidParam;
        if (ensureLoaded() != QC::Status::Success)
            return QC::Status::Error;

        const int index = findKey(key);
        if (index < 0)
            return QC::Status::NotFound;

        const QC::usize i = static_cast<QC::usize>(index);
        for (QC::usize j = i + 1; j < m_items.size(); ++j)
            m_items[j - 1] = m_items[j];
        if (!m_items.empty())
            m_items.pop_back();
        return QC::Status::Success;
    }

    QC::usize Store::list(Entry *outEntries, QC::usize cap) const
    {
        if (ensureLoaded() != QC::Status::Success)
            return 0;

        const QC::usize count = m_items.size();
        if (!outEntries || cap == 0)
            return count;

        QC::usize copy = (count < cap) ? count : cap;
        for (QC::usize i = 0; i < copy; ++i)
        {
            QC::String::memset(&outEntries[i], 0, sizeof(Entry));
            QC::String::strncpy(outEntries[i].key, m_items[i].key, sizeof(outEntries[i].key) - 1);
            QC::String::strncpy(outEntries[i].value, m_items[i].value, sizeof(outEntries[i].value) - 1);
        }
        return count;
    }
}
