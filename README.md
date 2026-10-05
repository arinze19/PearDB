# pearDB

A key-value storage engine built from scratch in C++17, inspired by how production databases like LevelDB and RocksDB work internally.

## Overview

pearDB implements a **Log-Structured Merge Tree (LSM Tree)** storage engine with a pluggable indexing layer that supports both a **Hash Index** and a **B-Tree Index** — switchable at runtime. The project was built to deeply understand how databases persist, index, and recover data at the systems level.

```
┌─────────────────────────────────────────────────────┐
│                   pearDB Architecture               │
│                                                     │
│  CLI / API                                          │
│      │                                              │
│      ▼                                              │
│  ┌────────────────────────────────────┐             │
│  │            pearDB Core             │             │
│  │                                    │             │
│  │  ┌──────────┐    ┌─────────────┐   │             │
│  │  │ Memtable │    │  WAL (log)  │   │             │
│  │  │(in-memory│    │  (on-disk)  │   │             │
│  │  │  sorted) │    │             │   │             │
│  │  └────┬─────┘    └─────────────┘   │             │
│  │       │ flush                      │             │
│  │       ▼                            │             │
│  │  ┌─────────────────────────────┐   │             │
│  │  │      Segment Manager        │   │             │
│  │  │  seg_0000 seg_0001 seg_0002 │   │             │
│  │  └─────────────────────────────┘   │             │
│  │                                    │             │
│  │  ┌──────────────────────────────┐  │             │
│  │  │     Pluggable Index          │  │             │
│  │  │  HashIndex  │  BTreeIndex    │  │             │
│  │  └──────────────────────────────┘  │             │
│  └────────────────────────────────────┘             │
└─────────────────────────────────────────────────────┘
```

---

## Features

- **Append-only storage** — writes never mutate existing data, ensuring durability
- **LSM Tree write path** — writes buffer in a sorted memtable, flushed to disk as immutable segments
- **Pluggable index layer** — swap between Hash and B-Tree index at startup
- **Segment compaction** — background merging removes stale and deleted records
- **Write-Ahead Log (WAL)** — crash recovery replays uncommitted memtable entries on restart
- **Tombstone deletes** — deletions are recorded as markers, cleaned up during compaction
- **Range queries** — supported via B-Tree index (`range <start> <end>`)
- **Persistent index rebuild** — index reconstructed from segment files on every startup

---

## Getting Started

### Prerequisites

- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.15+

### Build

```bash
git clone https://github.com/arinze19/pearDB
cd pearDB
mkdir build && cd build
cmake ..
make
```

### Run

```bash
# Hash index (default — faster exact lookups)
./pearDB --index=hash

# B-Tree index (supports range queries)
./pearDB --index=btree
```

---

## Commands

| Command | Description |
|---------|-------------|
| `set <key> <value>` | Write a key-value pair |
| `get <key>` | Read a value |
| `del <key>` | Delete a key |
| `keys` | List all keys (memtable + disk) |
| `range <start> <end>` | Range scan (B-Tree only) |
| `flush` | Manually flush memtable to disk |
| `compact` | Merge and clean all segments |
| `stats` | Show DB statistics |
| `crash` | Simulate a crash (for WAL recovery testing) |
| `exit` | Shutdown gracefully (auto-flushes) |

---

## Architecture Deep Dive

### Write Path

```
put("name", "Arinze")
        │
        ├─► 1. WAL.logPut()       → wal.log on disk   (durable!)
        │
        └─► 2. Memtable.put()     → in-memory sorted map
                    │
                    │ (when full)
                    ▼
             3. Flush to Segment  → seg_000N.seg on disk
             4. Update Index      → key → (segment_idx, offset)
             5. WAL.clear()       → safe to discard log
```

### Read Path

```
get("name")
        │
        ├─► 1. Check Memtable     → found? return immediately
        │         (tombstone?)    → return nil (deleted)
        │         (not found?)    → continue
        │
        ├─► 2. Check Index        → get (segment_idx, offset)
        │         (not found?)    → return nil
        │
        └─► 3. Read Segment       → seek to offset, decode record
```

### Crash Recovery

```
Normal shutdown:          Crash (kill -9):
  flush memtable            memtable lost!
  clear WAL                 WAL preserved on disk
  exit                      
                          On restart:
                            load segments → rebuild index
                            detect WAL has entries
                            replay WAL → restore memtable
                            continue normally
```

### Compaction

```
Before:                       After:
  seg_0000: [name=Arinze]       seg_0000: [age=25]
            [city=Amsterdam]              [name=John]  ← latest only
  seg_0001: [age=25]
            [name=John]      Tombstones removed.
  seg_0002: [city=TOMBSTONE]  Duplicates removed.
                              One clean segment.
```

---

## Index Comparison

| Feature | Hash Index | B-Tree Index |
|---------|-----------|--------------|
| Exact lookup | O(1) average | O(log n) |
| Range queries | ❌ Not supported | ✅ Supported |
| Key ordering | ❌ Unordered | ✅ Always sorted |
| Memory usage | Higher | Lower |
| Write overhead | Lower | Higher (rebalancing) |
| Real-world analog | Redis, DynamoDB | PostgreSQL, MySQL |

---

## Benchmark Results

> Benchmarked on Apple M1, 8GB RAM, macOS 14. `-O2` optimizations enabled.

### 100,000 Operations

| Benchmark | Hash Index | B-Tree Index | Δ |
|-----------|-----------|--------------|---|
| Sequential Writes | ~850,000 ops/s | ~620,000 ops/s | Hash 1.37x faster |
| Sequential Reads | ~920,000 ops/s | ~710,000 ops/s | Hash 1.30x faster |
| Mixed (50R/50W) | ~880,000 ops/s | ~650,000 ops/s | Hash 1.35x faster |

### Scale Test

| Operations | Index | Throughput |
|------------|-------|-----------|
| 1,000,000 writes | Hash | ~800,000 ops/s |

> B-Tree's O(log n) overhead becomes measurable at scale, but its range query support makes it the right choice for ordered workloads.

---

## Running Tests

```bash
cd build
./pearDB_tests

# Expected:
# Passed: 24
# Failed: 0
# Total:  24
```

### Running Benchmarks

```bash
./pearDB_bench
```

---

## Design Decisions

### Why LSM Tree over B-Tree storage?

LSM trees convert **random writes** (slow, ~1ms disk seek) into **sequential writes** (fast, ~100μs). The memtable batches writes in memory; the flush is one sequential disk write. This is why write-heavy databases like Cassandra, RocksDB, and LevelDB use LSM trees.

### Why append-only segments?

Mutating existing records requires reading, modifying, and rewriting — expensive for spinning disks and complex for concurrent access. Append-only means writes are always sequential and segments are immutable, making compaction safe to run in the background without locking reads.

### Why WAL before memtable?

If we wrote to the memtable first and crashed, all buffered writes would be permanently lost. Writing to the WAL first (durable on disk) means we can always replay it on restart. The WAL is only cleared after the memtable successfully flushes to a segment — ensuring atomicity.

### Why encode (segment_idx, offset) into one size_t?

The abstract `Index` interface stores one `size_t` per key. Encoding both the segment index (upper 32 bits) and byte offset (lower 32 bits) into a single value lets us track multi-segment locations without changing the interface contract.

---

## File Format

### Segment record on disk

```
[key_size: 4 bytes][value_size: 4 bytes][tombstone: 1 byte][key bytes][value bytes]
```

### WAL entry on disk

```
[op_type: 1 byte (0x01=PUT, 0x02=DEL)][key_size: 4 bytes][value_size: 4 bytes][key bytes][value bytes]
```

---

## Project Structure

```
pearDB/
├── src/
│   ├── main.cpp                  # CLI entry point
│   ├── db.h / db.cpp             # Core database — orchestrates all components
│   ├── storage/
│   │   ├── segment.h/.cpp        # Single segment file (append-only)
│   │   ├── segment_manager.h/.cpp # Multi-segment management + compaction
│   │   ├── memtable.h/.cpp       # In-memory sorted write buffer
│   │   └── wal.h/.cpp            # Write-ahead log (crash recovery)
│   └── index/
│       ├── index.h               # Abstract index interface
│       ├── hash_index.h/.cpp     # O(1) hash table index
│       └── btree_index.h/.cpp    # O(log n) sorted tree index + range queries
├── tests/
│   ├── test_framework.h          # Minimal test framework (no dependencies)
│   ├── test_segment.cpp
│   ├── test_memtable.cpp
│   ├── test_wal.cpp
│   ├── test_db.cpp               # Integration tests
│   └── test_runner.cpp
├── benchmarks/
│   └── benchmark.cpp             # Hash vs B-Tree performance comparison
└── CMakeLists.txt
```

---

## Resources 
1. [B-trees and database indexes](https://planetscale.com/blog/btrees-and-database-indexes), By [planet scale](https://planetscale.com).
2. [Build your own database](https://www.nan.fyi/database), By [Nanda Syahrasyad](https://x.com/nandafyi).
3. [Database recovery demystified: understanding aries from first principles](https://yashagw.github.io/blog/db-recovery/), By [Yash Agarwal](https://yashagw.github.io/)

---

## License

MIT

