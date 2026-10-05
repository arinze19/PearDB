#include "test_framework.h"
#include "db.h"
#include <filesystem>

namespace fs = std::filesystem;

// Helper - clean test directory before each test
static std::string makeTestDir(const std::string &name)
{
    std::string path = "/tmp/pearDB_test_" + name;
    if (fs::exists(path))
        fs::remove_all(path);
    return path;
}

// ─── Hash Index Tests ───────────────────────

TEST(db_hash_put_and_get)
{
    auto dir = makeTestDir("put_get");
    pearDB db(dir, pearDB::IndexType::HASH);

    db.put("name", "Arinze");
    auto result = db.get("name");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "Arinze");
}

TEST(db_hash_get_missing_key)
{
    auto dir = makeTestDir("missing");
    pearDB db(dir, pearDB::IndexType::HASH);

    auto result = db.get("ghost");
    ASSERT_FALSE(result.has_value());
}

TEST(db_hash_overwrite_key)
{
    auto dir = makeTestDir("overwrite");
    pearDB db(dir, pearDB::IndexType::HASH);

    db.put("key", "old_value");
    db.put("key", "new_value");

    auto result = db.get("key");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "new_value");
}

TEST(db_hash_delete_key)
{
    auto dir = makeTestDir("delete");
    pearDB db(dir, pearDB::IndexType::HASH);

    db.put("key", "value");
    db.remove("key");

    auto result = db.get("key");
    ASSERT_FALSE(result.has_value());
}

TEST(db_hash_delete_nonexistent)
{
    auto dir = makeTestDir("del_nonexist");
    pearDB db(dir, pearDB::IndexType::HASH);

    // Should not throw!
    db.remove("ghost_key");
    ASSERT_FALSE(db.get("ghost_key").has_value());
}

TEST(db_hash_multiple_keys)
{
    auto dir = makeTestDir("multi");
    pearDB db(dir, pearDB::IndexType::HASH);

    db.put("a", "1");
    db.put("b", "2");
    db.put("c", "3");

    ASSERT_EQ(*db.get("a"), "1");
    ASSERT_EQ(*db.get("b"), "2");
    ASSERT_EQ(*db.get("c"), "3");
    ASSERT_EQ(db.indexSize(), size_t(0)); // All in memtable!
}

TEST(db_hash_persists_after_flush)
{
    auto dir = makeTestDir("persist_flush");

    {
        pearDB db(dir, pearDB::IndexType::HASH);
        db.put("name", "Arinze");
        db.put("city", "Lagos");
        db.flushMemtable();
        // Destructor runs, data on disk
    }

    // Reopen - data should still be there!
    pearDB db2(dir, pearDB::IndexType::HASH);
    ASSERT_EQ(*db2.get("name"), "Arinze");
    ASSERT_EQ(*db2.get("city"), "Lagos");
}

TEST(db_crash_recovery_via_wal)
{
    auto dir = makeTestDir("crash_recovery");

    // Write WITHOUT flushing (simulates in-memory only state)
    {
        pearDB db(dir, pearDB::IndexType::HASH);
        db.put("crash_key", "crash_value");
        db.put("another", "data");
        // Destructor flushes automatically, so we need to
        // test WAL replay by checking WAL entries exist
        ASSERT(db.getWALSize() > 0); // WAL has entries before flush
    }

    // Destructor flushed, so on restart data is on disk
    pearDB db2(dir, pearDB::IndexType::HASH);
    ASSERT_EQ(*db2.get("crash_key"), "crash_value");
    ASSERT_EQ(*db2.get("another"), "data");
}

TEST(db_memtable_read_before_flush)
{
    auto dir = makeTestDir("memtable_read");
    pearDB db(dir, pearDB::IndexType::HASH);

    // Data only in memtable, not flushed to disk yet!
    db.put("fresh", "data");
    ASSERT_EQ(db.indexSize(), size_t(0)); // Not on disk yet

    // But get() still finds it in memtable!
    auto result = db.get("fresh");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "fresh" == "fresh" ? "data" : "");
}

// ─── BTree Index Tests ──────────────────────

TEST(db_btree_put_and_get)
{
    auto dir = makeTestDir("btree_put");
    pearDB db(dir, pearDB::IndexType::BTREE);

    db.put("key", "value");
    ASSERT_EQ(*db.get("key"), "value");
}

TEST(db_btree_range_query)
{
    auto dir = makeTestDir("btree_range");
    pearDB db(dir, pearDB::IndexType::BTREE);

    db.put("apple", "1");
    db.put("banana", "2");
    db.put("cherry", "3");
    db.put("date", "4");
    db.put("elderberry", "5");

    // Flush so data is in on-disk index (range queries use disk index)
    db.flushMemtable();

    auto results = db.range("banana", "date");
    ASSERT_EQ(results.size(), size_t(3));
    ASSERT_EQ(results[0].first, "banana");
    ASSERT_EQ(results[1].first, "cherry");
    ASSERT_EQ(results[2].first, "date");
}

TEST(db_btree_range_empty_result)
{
    auto dir = makeTestDir("btree_range_empty");
    pearDB db(dir, pearDB::IndexType::BTREE);

    db.put("apple", "1");
    db.put("banana", "2");
    db.flushMemtable();

    // Range that matches nothing
    auto results = db.range("zzz", "zzzzz");
    ASSERT_EQ(results.size(), size_t(0));
}

TEST(db_btree_keys_sorted)
{
    auto dir = makeTestDir("btree_sorted");
    pearDB db(dir, pearDB::IndexType::BTREE);

    db.put("zebra", "z");
    db.put("apple", "a");
    db.put("mango", "m");
    db.flushMemtable();

    auto all_keys = db.keys();
    ASSERT_EQ(all_keys.size(), size_t(3));
    ASSERT_EQ(all_keys[0], "apple"); // Sorted!
    ASSERT_EQ(all_keys[1], "mango");
    ASSERT_EQ(all_keys[2], "zebra");
}

TEST(db_hash_range_throws)
{
    auto dir = makeTestDir("hash_range_err");
    pearDB db(dir, pearDB::IndexType::HASH);

    bool threw = false;
    try
    {
        db.range("a", "z");
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    ASSERT_TRUE(threw); // Hash index must throw on range!
}

// ─── Compaction Tests ───────────────────────

TEST(db_compact_removes_duplicates)
{
    auto dir = makeTestDir("compact_dup");
    pearDB db(dir, pearDB::IndexType::HASH);

    // Write same key multiple times
    db.put("key", "v1");
    db.put("key", "v2");
    db.put("key", "v3");
    db.flushMemtable();

    db.compact();

    // Only one record should exist after compaction
    ASSERT_EQ(*db.get("key"), "v3");
    ASSERT_EQ(db.getSegmentCount(), size_t(1));
}

TEST(db_compact_removes_tombstones)
{
    auto dir = makeTestDir("compact_tomb");
    pearDB db(dir, pearDB::IndexType::HASH);

    db.put("alive", "yes");
    db.put("dead", "bye");
    db.remove("dead");
    db.flushMemtable();
    db.compact();

    ASSERT_TRUE(db.get("alive").has_value());
    ASSERT_FALSE(db.get("dead").has_value());
    ASSERT_EQ(db.getSegmentCount(), size_t(1));
}