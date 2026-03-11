// QFileSystem Cache - Implementation
// Namespace: QFS

#include "QFSCache.h"
#include "QFSFAT32.h"
#include "QKMemHeap.h"
#include "QCString.h"

namespace QFS
{

    Cache::Cache(BlockDevice *device, QC::usize cacheSize, QC::usize sectorSize)
        : m_device(device), m_sectorSize(sectorSize), m_cacheSize(cacheSize), m_lruHead(nullptr), m_lruTail(nullptr), m_hits(0), m_misses(0)
    {

        m_entryCount = cacheSize / sectorSize;

        // Allocate entries
        m_entries = static_cast<CacheEntry *>(
            QK::Memory::Heap::instance().allocate(m_entryCount * sizeof(CacheEntry)));

        // Allocate data buffers and initialize entries
        for (QC::usize i = 0; i < m_entryCount; ++i)
        {
            m_entries[i].data = static_cast<QC::u8 *>(
                QK::Memory::Heap::instance().allocate(sectorSize));
            m_entries[i].sector = 0xFFFFFFFFFFFFFFFFULL; // Invalid
            m_entries[i].dirty = false;
            m_entries[i].lastAccess = 0;
            m_entries[i].next = nullptr;
            m_entries[i].prev = nullptr;
        }

        // Clear hash table
        QC::String::memset(m_hashTable, 0, sizeof(m_hashTable));
    }

    Cache::~Cache()
    {
        flush();

        for (QC::usize i = 0; i < m_entryCount; ++i)
        {
            QK::Memory::Heap::instance().free(m_entries[i].data);
        }
        QK::Memory::Heap::instance().free(m_entries);
    }

    CacheEntry *Cache::findEntry(QC::u64 sector)
    {
        QC::usize hash = sector % HASH_SIZE;
        CacheEntry *entry = m_hashTable[hash];

        while (entry)
        {
            if (entry->sector == sector)
            {
                return entry;
            }
            entry = entry->next;
        }

        return nullptr;
    }

    CacheEntry *Cache::allocateEntry()
    {
        // Find a free entry or evict LRU
        for (QC::usize i = 0; i < m_entryCount; ++i)
        {
            if (m_entries[i].sector == 0xFFFFFFFFFFFFFFFFULL)
            {
                return &m_entries[i];
            }
        }

        // Evict LRU
        evictLRU();
        return m_lruTail;
    }

    void Cache::touchEntry(CacheEntry *entry)
    {
        // Move to head of LRU list
        if (entry == m_lruHead)
            return;

        // Remove from current position
        if (entry->prev)
            entry->prev->next = entry->next;
        if (entry->next)
            entry->next->prev = entry->prev;
        if (entry == m_lruTail)
            m_lruTail = entry->prev;

        // Insert at head
        entry->prev = nullptr;
        entry->next = m_lruHead;
        if (m_lruHead)
            m_lruHead->prev = entry;
        m_lruHead = entry;
        if (!m_lruTail)
            m_lruTail = entry;
    }

    void Cache::evictLRU()
    {
        if (!m_lruTail || !m_lruTail->dirty)
            return;

        // Write back if dirty
        if (m_lruTail->dirty)
        {
            m_device->writeSector(m_lruTail->sector, m_lruTail->data);
            m_lruTail->dirty = false;
        }

        // Remove from hash table
        QC::usize hash = m_lruTail->sector % HASH_SIZE;
        CacheEntry *entry = m_hashTable[hash];
        CacheEntry *prev = nullptr;

        while (entry)
        {
            if (entry == m_lruTail)
            {
                if (prev)
                {
                    prev->next = entry->next;
                }
                else
                {
                    m_hashTable[hash] = entry->next;
                }
                break;
            }
            prev = entry;
            entry = entry->next;
        }

        m_lruTail->sector = 0xFFFFFFFFFFFFFFFFULL;
    }

    QC::Status Cache::read(QC::u64 sector, void *buffer)
    {
        CacheEntry *entry = findEntry(sector);

        if (entry)
        {
            ++m_hits;
            touchEntry(entry);
            QC::String::memcpy(buffer, entry->data, m_sectorSize);
            return QC::Status::Success;
        }

        ++m_misses;

        // Allocate entry and read from device
        entry = allocateEntry();
        entry->sector = sector;

        QC::Status status = m_device->readSector(sector, entry->data);
        if (status != QC::Status::Success)
        {
            entry->sector = 0xFFFFFFFFFFFFFFFFULL;
            return status;
        }

        // Add to hash table
        QC::usize hash = sector % HASH_SIZE;
        entry->next = m_hashTable[hash];
        m_hashTable[hash] = entry;

        touchEntry(entry);
        QC::String::memcpy(buffer, entry->data, m_sectorSize);

        return QC::Status::Success;
    }

    QC::Status Cache::write(QC::u64 sector, const void *buffer)
    {
        CacheEntry *entry = findEntry(sector);

        if (!entry)
        {
            entry = allocateEntry();
            entry->sector = sector;

            // Add to hash table
            QC::usize hash = sector % HASH_SIZE;
            entry->next = m_hashTable[hash];
            m_hashTable[hash] = entry;
        }

        QC::String::memcpy(entry->data, buffer, m_sectorSize);
        entry->dirty = true;
        touchEntry(entry);

        return QC::Status::Success;
    }

    void Cache::flush()
    {
        for (QC::usize i = 0; i < m_entryCount; ++i)
        {
            if (m_entries[i].dirty && m_entries[i].sector != 0xFFFFFFFFFFFFFFFFULL)
            {
                m_device->writeSector(m_entries[i].sector, m_entries[i].data);
                m_entries[i].dirty = false;
            }
        }
    }

    void Cache::invalidate()
    {
        flush();

        for (QC::usize i = 0; i < m_entryCount; ++i)
        {
            m_entries[i].sector = 0xFFFFFFFFFFFFFFFFULL;
        }

        QC::String::memset(m_hashTable, 0, sizeof(m_hashTable));
        m_lruHead = nullptr;
        m_lruTail = nullptr;
    }

    void Cache::invalidateSector(QC::u64 sector)
    {
        CacheEntry *entry = findEntry(sector);
        if (entry)
        {
            entry->sector = 0xFFFFFFFFFFFFFFFFULL;
            entry->dirty = false;
        }
    }

} // namespace QFS
