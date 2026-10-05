#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <iomanip>
#include <filesystem>
#include "db.h"

// ─────────────────────────────────────────────
// Timer utility
// ─────────────────────────────────────────────
struct Timer {
    std::chrono::high_resolution_clock::time_point start;

    Timer() : start(std::chrono::high_resolution_clock::now()) {}

    double elapsedMs() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    double elapsedUs() const { return elapsedMs() * 1000.0; }
};

// ─────────────────────────────────────────────
// Generate random keys and values
// ─────────────────────────────────────────────
std::vector<std::pair<std::string, std::string>> generateData(size_t n) {
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int> dist(100000, 999999);

    std::vector<std::pair<std::string, std::string>> data;
    data.reserve(n);

    for (size_t i = 0; i < n; i++) {
        data.emplace_back(
            "key_" + std::to_string(dist(rng)),
            "value_" + std::to_string(i)
        );
    }
    return data;
}

// ─────────────────────────────────────────────
// Print result row
// ─────────────────────────────────────────────
void printResult(
    const std::string& label,
    size_t ops,
    double elapsed_ms
) {
    double ops_per_sec = (ops / elapsed_ms) * 1000.0;
    double ns_per_op   = (elapsed_ms / ops) * 1e6;

    std::cout << std::left  << std::setw(35) << label
              << std::right << std::setw(10) << ops        << " ops"
              << std::right << std::setw(10) << std::fixed
                            << std::setprecision(1)
                            << ops_per_sec                  << " ops/s"
              << std::right << std::setw(10) << std::fixed
                            << std::setprecision(2)
                            << ns_per_op                    << " ns/op"
              << "\n";
}

// ─────────────────────────────────────────────
// Run benchmark for a given config
// ─────────────────────────────────────────────
struct BenchResult {
    double write_ms;
    double read_ms;
    double mixed_ms;
    size_t ops;
};

BenchResult runBenchmark(
    const std::string& dir,
    pearDB::IndexType type,
    size_t num_ops,
    const std::vector<std::pair<std::string, std::string>>& data
) {
    if (std::filesystem::exists(dir)) std::filesystem::remove_all(dir);
    pearDB db(dir, type);

    BenchResult result;
    result.ops = num_ops;

    // ── Sequential Writes ──
    {
        Timer t;
        for (size_t i = 0; i < num_ops; i++) {
            db.put(data[i].first, data[i].second);
        }
        db.flushMemtable();
        result.write_ms = t.elapsedMs();
    }

    // ── Sequential Reads ──
    {
        Timer t;
        size_t hits = 0;
        for (size_t i = 0; i < num_ops; i++) {
            if (db.get(data[i].first).has_value()) hits++;
        }
        result.read_ms = t.elapsedMs();
    }

    // ── Mixed (50% read / 50% write) ──
    {
        Timer t;
        for (size_t i = 0; i < num_ops; i++) {
            if (i % 2 == 0) {
                db.put(data[i].first, "updated_" + std::to_string(i));
            } else {
                db.get(data[i].first);
            }
        }
        result.mixed_ms = t.elapsedMs();
    }

    return result;
}

// ─────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────
int main() {
    const size_t NUM_OPS = 100'000;

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║               pearDB Benchmark Suite                     ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Generating " << NUM_OPS << " random key-value pairs...\n";
    auto data = generateData(NUM_OPS);

    std::cout << "\n";
    std::cout << std::left  << std::setw(35) << "Benchmark"
              << std::right << std::setw(15) << "Operations"
              << std::right << std::setw(15) << "Throughput"
              << std::right << std::setw(15) << "Latency"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    // ─── Hash Index ───
    std::cout << "\n[Hash Index]\n";
    auto hash_result = runBenchmark("/tmp/bench_hash", pearDB::IndexType::HASH, NUM_OPS, data);
    printResult("  Sequential Writes",  NUM_OPS, hash_result.write_ms);
    printResult("  Sequential Reads",   NUM_OPS, hash_result.read_ms);
    printResult("  Mixed (50R/50W)",    NUM_OPS, hash_result.mixed_ms);

    // ─── BTree Index ───
    std::cout << "\n[BTree Index]\n";
    auto btree_result = runBenchmark("/tmp/bench_btree", pearDB::IndexType::BTREE, NUM_OPS, data);
    printResult("  Sequential Writes",  NUM_OPS, btree_result.write_ms);
    printResult("  Sequential Reads",   NUM_OPS, btree_result.read_ms);
    printResult("  Mixed (50R/50W)",    NUM_OPS, btree_result.mixed_ms);

    // ─── Comparison Summary ───
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "COMPARISON SUMMARY\n";
    std::cout << std::string(80, '=') << "\n";

    auto ratio = [](double a, double b) {
        return a < b ? (b/a) : -(a/b);
    };

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\nWrites:  Hash is "
              << std::abs(ratio(hash_result.write_ms, btree_result.write_ms))
              << "x " << (hash_result.write_ms < btree_result.write_ms ? "faster" : "slower")
              << " than BTree\n";

    std::cout << "Reads:   Hash is "
              << std::abs(ratio(hash_result.read_ms, btree_result.read_ms))
              << "x " << (hash_result.read_ms < btree_result.read_ms ? "faster" : "slower")
              << " than BTree\n";

    std::cout << "Mixed:   Hash is "
              << std::abs(ratio(hash_result.mixed_ms, btree_result.mixed_ms))
              << "x " << (hash_result.mixed_ms < btree_result.mixed_ms ? "faster" : "slower")
              << " than BTree\n";

    // ─── Scale test: 1M ops ───
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "SCALE TEST (1,000,000 writes - Hash only)\n";
    std::cout << std::string(80, '=') << "\n\n";

    const size_t LARGE_OPS = 1'000'000;
    auto large_data = generateData(LARGE_OPS);

    if (std::filesystem::exists("/tmp/bench_large")) std::filesystem::remove_all("/tmp/bench_large");
    pearDB large_db("/tmp/bench_large", pearDB::IndexType::HASH);

    Timer large_timer;
    for (size_t i = 0; i < LARGE_OPS; i++) {
        large_db.put(large_data[i].first, large_data[i].second);
        if (i % 100000 == 0 && i > 0) {
            large_db.flushMemtable();
            std::cout << "  Progress: " << i << " / " << LARGE_OPS << "\n";
        }
    }
    large_db.flushMemtable();
    double large_ms = large_timer.elapsedMs();

    printResult("  1M Sequential Writes (Hash)", LARGE_OPS, large_ms);

    std::cout << "\n✅ Benchmark complete!\n\n";
    return 0;
}