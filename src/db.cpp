#include "db.h"
#include "index/btree_index.h"
#include "index/hash_index.h"
#include "storage/wal.h"
#include <filesystem>
#include <iostream>
#include <stdexcept> // provides cpp exception type: std::runtime_error

// TODO: Insepct
size_t pearDB::encodeLocation(size_t segment_idx, size_t offset) const
{
    return (segment_idx << 32) | (offset & 0xFFFFFFFF);
}

std::pair<size_t, size_t> pearDB::decodeLocation(size_t encoded) const
{
    size_t segment_idx = encoded >> 32;
    size_t offset = encoded & 0xFFFFFFFF;
    return {segment_idx, offset};
}

std::unique_ptr<Index> pearDB::createIndex(IndexType type)
{
    switch (type)
    {
    case IndexType::HASH:
        return std::make_unique<HashIndex>();
    case IndexType::BTREE:
        return std::make_unique<BTreeIndex>();
    default:
        std::cout << "Index type is unknown - Defaulting to HashIndex" << std::endl;
        return std::make_unique<HashIndex>();
    }
}

pearDB::pearDB(const std::string &dir, IndexType type) : data_dir(dir)
{
    if (!std::filesystem::exists(data_dir))
    {
        std::filesystem::create_directories(data_dir);
    }

    index = createIndex(type);
    segment_manager = std::make_unique<SegmentManager>(data_dir);
    memtable = std::make_unique<Memtable>();
    wal = std::make_unique<WAL>(data_dir + "/wal.log");

    buildIndex();

    recoverFromWAL();

    std::cout << "[pearDB] Opened with "
              << (type == IndexType::HASH ? "HashIndex" : "BTreeIndex")
              << " | Keys: " << index->size() << " keys loaded"
              << " | Segments: " << segment_manager->segmentCount()
              << " | Memtable: " << memtable->sizeBytes() << " Bytes"
              << " | WAL: " << wal->size() << " Bytes" << std::endl;
}

void pearDB::put(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    wal->logPut(key, value);

    memtable->put(key, value);

    if (memtable->isFull())
    {
        std::cout << "[pearDB] Memtable full, flushing to disk... " << std::endl;

        flushMemtableInternal();
    }
}

std::optional<std::string> pearDB::get(const std::string &key)
{
    if (memtable->contains(key))
    {
        return memtable->get(key);
    }

    // index scan
    auto encoded = index->get(key);

    if (!encoded.has_value())
    {
        std::cout << "[pearDB] key not found: " << key << "\n";
        return std::nullopt;
    };

    auto [segment_idx, offset] = decodeLocation(*encoded);

    auto record = segment_manager->read(segment_idx, offset);

    if (record->tombstone || !record.has_value())
    {
        std::cout << "[pearDB] key not found: " << key << "\n";
        return std::nullopt;
    };

    return record->value;
}

bool pearDB::remove(const std::string &key)
{
    std::lock_guard<std::mutex> lock(db_mutex);

    if (!memtable->contains(key) && !index->contains(key))
    {
        std::cout << "[pearDB] key not found..." << std::endl;
        return false;
    }

    wal->logDelete(key);

    memtable->remove(key); // when flushed tombstone will supress on disk record

    if (index->contains(key))
    {
        index->remove(key);
    }

    if (memtable->isFull())
    {
        flushMemtableInternal();
    }

    return true;
}

void pearDB::flushMemtableInternal()
{
    if (memtable->isEmpty())
        return;

    std::vector<Record> records = memtable->flush();

    for (const auto &record : records)
    {
        auto [segment_idx, offset] = segment_manager->write(record);

        if (record.tombstone)
        {
            index->remove(record.key);
        }
        else
        {
            index->put(record.key, encodeLocation(segment_idx, offset));
        }
    }

    std::cout << "[pearDB] Memtable flushed successfully..." << std::endl;

    memtable->clear();

    wal->clear();
}

// TODO: why expose flush memtable - why not just call flush memtable internal
void pearDB::flushMemtable()
{
    std::lock_guard<std::mutex> lock(db_mutex);
    flushMemtableInternal();
}

std::vector<std::pair<std::string, std::string>> pearDB::range(const std::string &start, const std::string &end)
{
    auto *btree = dynamic_cast<BTreeIndex *>(index.get());

    if (!btree)
    {
        throw std::runtime_error(
            "Range queries require BTreeIndex! "
            "Restart with --index=btree");
    }

    auto pairs = btree->range(start, end);

    // reserver space for incoming items
    std::map<std::string, std::string> result; // why move from std::vector<std::pair<std::string, std::string>>

    for (const auto &[key, encoded] : pairs)
    {
        if (memtable->contains(key))
        {
            auto entity = memtable->get(key);

            if (entity.has_value())
                result[key] = *entity;
        }
        else
        {
            auto [segment_idx, offset] = decodeLocation(encoded);
            auto record = segment_manager->read(segment_idx, offset);

            if (record.has_value() && !record->tombstone)
            {
                result[key] = record->value;
            }
        }
    }

    // ====================== TODO: Inspect ======================
    // Also check memtable for keys in range not yet on disk
    // (These won't be in the btree index yet!)
    // Note: This is a simplified approach - production LSMs have
    // a separate memtable iterator for this
    auto flushed = memtable->flush();
    for (const auto &record : flushed)
    {
        if (!record.tombstone && record.key >= start && record.key <= end)
        {
            result[record.key] = record.value;
        }
    }
    // ==================================================================

    return {result.begin(), result.end()}; // TODO: Inspect
}

void pearDB::buildIndex()
{
    auto all_records = segment_manager->readAll();

    for (const auto &[segment_idx, offset, record] : all_records)
    {
        if (record.tombstone)
        {
            index->remove(record.key);
        }
        else
        {
            index->put(record.key, encodeLocation(segment_idx, offset));
        }
    }
}

void pearDB::recoverFromWAL()
{
    if (wal->isEmpty())
        return;

    std::cout << "[WAL] Crash recovery detected! Replaying WAL..." << std::endl;

    auto records = wal->replay();

    for (const auto &record : records)
    {
        if (record.tombstone)
        {
            memtable->remove(record.key);
            index->remove(record.key);
        }
        else
        {
            memtable->put(record.key, record.value);
        }
    }

    std::cout << "[WAL] Recovery complete! "
              << records.size() << " entries restored to memtable\n";

    // clear wal after memtable flushed
}

void pearDB::compact()
{
    {
        std::lock_guard<std::mutex> lock(db_mutex);
        flushMemtableInternal();
    }
    segment_manager->compact(*index);
}

size_t pearDB::indexSize() const
{
    return index->size();
}

size_t pearDB::getSegmentCount() const
{
    return segment_manager->segmentCount();
}

std::vector<std::string> pearDB::keys() const
{
    // get the keys in the index
    return index->keys();
}

size_t pearDB::getMemtableSize() const
{
    return memtable->sizeBytes();
}

size_t pearDB::getWALSize() const
{
    return wal->size();
}
