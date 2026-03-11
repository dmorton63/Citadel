#pragma once

// QFileSystem Cache - Block cache
// Namespace: QFS

#include "QCTypes.h"

namespace QFS
{

    class BlockDevice;

    struct CacheEntry
    {
        QC::u64 sector;
        QC::u8 *data;
        bool dirty;
        QC::u64 lastAccess;
        CacheEntry *next;
        CacheEntry *prev;
    };

    class Cache
    {
    public:
        Cache(BlockDevice *device, QC::usize cacheSize, QC::usize sectorSize);
        ~Cache();

        // Read/write through cache
        QC::Status read(QC::u64 sector, void *buffer);
        QC::Status write(QC::u64 sector, const void *buffer);

        // Cache management
        void flush();
        void invalidate();
        void invalidateSector(QC::u64 sector);

        // Statistics
        QC::u64 hits() const { return m_hits; }
        QC::u64 misses() const { return m_misses; }

    private:
        CacheEntry *findEntry(QC::u64 sector);
        CacheEntry *allocateEntry();
        void touchEntry(CacheEntry *entry);
        void evictLRU();

        BlockDevice *m_device;
        QC::usize m_sectorSize;
        QC::usize m_cacheSize;
        QC::usize m_entryCount;

        CacheEntry *m_entries;
        CacheEntry *m_lruHead;
        CacheEntry *m_lruTail;

        // Hash table for O(1) lookup
        static constexpr QC::usize HASH_SIZE = 256;
        CacheEntry *m_hashTable[HASH_SIZE];

        QC::u64 m_hits;
        QC::u64 m_misses;
    };

} // namespace QFS
