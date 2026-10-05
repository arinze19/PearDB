#pragma once

#include <memory>
#include <optional>
#include <string>
#include "storage/segment_manager.h"
#include "storage/memtable.h"
#include "storage/wal.h"
#include "index/hash_index.h"

class pearDB
{
public:
    // Determine index type to use for DB
    enum class IndexType
    {
        HASH,
        BTREE
    };

    explicit pearDB(const std::string &data_dir, IndexType index = IndexType::HASH);

    /**
     * If your class is meant to be inherited from,
     * you must define a virtual destructor so derived objects
     * are destroyed properly through base-class pointers:
     */
    ~pearDB() = default;

    void put(const std::string &key, const std::string &value);

    std::optional<std::string> get(const std::string &key);

    bool remove(const std::string &key);

    std::vector<std::pair<std::string, std::string>> range(const std::string &start, const std::string &end);

    size_t indexSize() const; // TODO: convert to getIndexSize to maintain consistency

    std::vector<std::string> keys() const;

    // getter function to return private variables
    // by returning here; we explicitly tell the program not to bother to redefine in .cpp file?
    // common for small getters
    IndexType getIndexType() const { return index_type; };

    void compact(); // Manually trigger compaction if needed (auto-runs in background)

    void flushMemtable(); // Manually flush memtable

    size_t getSegmentCount() const;

    size_t getWALSize() const;

    size_t getMemtableSize() const;

private:
    std::string data_dir;
    // with following setup we can opt to
    // create a segment manager at another part in our program initializer list
    // taking advantage of unique_ptr as well so we can dynamically create/delete segments when compacting
    std::unique_ptr<SegmentManager> segment_manager;
    std::unique_ptr<WAL> wal;
    std::unique_ptr<Index> index;
    std::unique_ptr<Memtable> memtable;
    IndexType index_type;
    mutable std::mutex db_mutex;

    // Index now tracks <segment_idx, offset>
    // we encode both into a single size_t for simplicity
    // upper 32 bits = segment_idx | lower 32 bits = offset
    size_t encodeLocation(size_t segment_idx, size_t offset) const;
    std::pair<size_t, size_t> decodeLocation(size_t encoded) const;

    void recoverFromWAL();

    void buildIndex();

    void flushMemtableInternal(); // Why not same as call in public

    // TODO: static ~ what is factory method
    static std::unique_ptr<Index> createIndex(IndexType type);
};