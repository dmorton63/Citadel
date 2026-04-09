#pragma once

#include "QCTypes.h"
#include "QCVector.h"

namespace QK::Db
{
    struct Entry
    {
        char key[48] = {0};
        char value[192] = {0};
    };

    class Store
    {
    public:
        static Store &instance();

        QC::Status load(const char *path = "/system/db/CITADEL.DB");
        QC::Status save();

        QC::Status set(const char *key, const char *value);
        QC::Status get(const char *key, char *outValue, QC::usize outCap) const;
        QC::Status erase(const char *key);

        QC::usize list(Entry *outEntries, QC::usize cap) const;
        const char *path() const { return m_path; }

    private:
        Store() = default;

        struct Item
        {
            char key[48] = {0};
            char value[192] = {0};
        };

        QC::Status ensureLoaded() const;
        int findKey(const char *key) const;

        mutable bool m_loaded = false;
        mutable char m_path[192] = {0};
        mutable QC::Vector<Item> m_items;
    };
}
