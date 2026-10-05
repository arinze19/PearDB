#include "test_framework.h"
#include "db.h"
#include "storage/wal.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

static void cleanupWAL(const std::string& path) {
    if (fs::exists(path)) fs::remove(path);
}

TEST(wal_log_put_and_replay) {
    std::string path = "/tmp/test_wal_put.log";
    cleanupWAL(path);

    {
        WAL wal(path);
        wal.logPut("name", "Arinze");
        wal.logPut("city", "Lagos");
    }

    // Replay in a new WAL instance (simulates restart!)
    WAL wal(path);
    auto records = wal.replay();

    ASSERT_EQ(records.size(), size_t(2));
    ASSERT_EQ(records[0].key,   "name");
    ASSERT_EQ(records[0].value, "Arinze");
    ASSERT_FALSE(records[0].tombstone);
    ASSERT_EQ(records[1].key,   "city");
    ASSERT_EQ(records[1].value, "Lagos");

    cleanupWAL(path);
}

TEST(wal_log_delete_and_replay) {
    std::string path = "/tmp/test_wal_del.log";
    cleanupWAL(path);

    {
        WAL wal(path);
        wal.logPut("key", "value");
        wal.logDelete("key");
    }

    WAL wal(path);
    auto records = wal.replay();

    ASSERT_EQ(records.size(), size_t(2));
    ASSERT_EQ(records[1].key, "key");
    ASSERT_TRUE(records[1].tombstone);  // Second entry is tombstone!

    cleanupWAL(path);
}

TEST(wal_clear_resets_state) {
    std::string path = "/tmp/test_wal_clear.log";
    cleanupWAL(path);

    WAL wal(path);
    wal.logPut("key", "value");

    ASSERT_FALSE(wal.isEmpty());
    ASSERT(wal.size() > 0);

    wal.clear();

    ASSERT_TRUE(wal.isEmpty());
    ASSERT_EQ(wal.size(), size_t(0));

    // Replay after clear returns empty
    auto records = wal.replay();
    ASSERT_EQ(records.size(), size_t(0));

    cleanupWAL(path);
}

TEST(wal_replay_empty_is_safe) {
    std::string path = "/tmp/test_wal_empty.log";
    cleanupWAL(path);

    WAL wal(path);
    auto records = wal.replay();

    ASSERT_EQ(records.size(), size_t(0));
    ASSERT_TRUE(wal.isEmpty());

    cleanupWAL(path);
}

TEST(wal_survives_simulated_crash) {
    std::string path = "/tmp/test_wal_crash.log";
    cleanupWAL(path);

    // Simulate process writing to WAL then "crashing" without clear
    {
        WAL wal(path);
        wal.logPut("before_crash", "important_data");
        wal.logPut("also_before",  "more_data");
        // NO clear() called - simulates crash!
    }

    // Simulate restart - WAL should still have entries!
    WAL wal(path);
    ASSERT_FALSE(wal.isEmpty());

    auto records = wal.replay();
    ASSERT_EQ(records.size(), size_t(2));
    ASSERT_EQ(records[0].key,   "before_crash");
    ASSERT_EQ(records[0].value, "important_data");
    ASSERT_EQ(records[1].key,   "also_before");

    cleanupWAL(path);
}

TEST(wal_size_increases_with_entries) {
    std::string path = "/tmp/test_wal_size.log";
    cleanupWAL(path);

    WAL wal(path);
    ASSERT_EQ(wal.size(), size_t(0));

    wal.logPut("k", "v");
    size_t size1 = wal.size();
    ASSERT(size1 > 0);

    wal.logPut("key2", "value2");
    ASSERT(wal.size() > size1);

    cleanupWAL(path);
}

TEST(wal_rebuild_memtable_median_latency_after_50_crashes)
{
    constexpr size_t record_count = 100000;
    constexpr size_t record_payload_bytes = 16;
    constexpr size_t memtable_entry_overhead = 64;
    constexpr size_t wal_record_overhead = 1 + sizeof(uint32_t) + sizeof(uint32_t);
    const std::string dir = "/tmp/pearDB_test_wal_recovery_latency";
    const std::string wal_path = dir + "/wal.log";

    if (fs::exists(dir))
        fs::remove_all(dir);
    fs::create_directories(dir);

    {
        WAL wal(wal_path);
        for (size_t i = 0; i < record_count; ++i)
        {
            std::ostringstream key;
            std::ostringstream value;
            key << 'k' << std::setw(7) << std::setfill('0') << i;
            value << 'v' << std::setw(7) << std::setfill('0') << i;
            wal.logPut(key.str(), value.str());
        }

        const size_t expected_size = record_count * (wal_record_overhead + record_payload_bytes);
        ASSERT_EQ(wal.size(), expected_size);
    }

    std::vector<long long> recovery_latencies_us;
    recovery_latencies_us.reserve(50);

    for (size_t crash = 0; crash < 50; ++crash)
    {
        const auto start = std::chrono::steady_clock::now();
        pearDB db(dir, pearDB::IndexType::HASH);
        const auto end = std::chrono::steady_clock::now();

        ASSERT_EQ(db.getMemtableSize(),
              record_count * (record_payload_bytes + memtable_entry_overhead));
        recovery_latencies_us.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
    }

    std::sort(recovery_latencies_us.begin(), recovery_latencies_us.end());
    const long long median_latency_us = recovery_latencies_us[recovery_latencies_us.size() / 2];
    std::cout << "[ PERF ] WAL recovery median over 50 crashes: "
              << median_latency_us << " us\n";

    fs::remove_all(dir);
}