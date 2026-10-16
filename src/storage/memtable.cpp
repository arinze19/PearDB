#include "memtable.h"
#include <stdexcept>
#include <iostream>

Memtable::Memtable(size_t size) : max_size(size), current_size(0) {}

void Memtable::put(const std::string &key, const std::string &value)
{
    auto it = table.find(key);

    if (it != table.end())
    {
        current_size -= entrySize(key, it->second.value);
    }

    table[key] = Entry{value, false};

    current_size += entrySize(key, value);
}

std::optional<std::string> Memtable::get(const std::string &key)
{
    auto it = table.find(key);

    if (it == table.end())
    {
        return std::nullopt;
    }

    if (it->second.tombstone)
    {
        return std::nullopt;
    }

    return it->second.value;
}

void Memtable::remove(const std::string &key)
{
    auto it = table.find(key);

    if (it != table.end())
    {
        current_size -= entrySize(key, it->second.value);
    }

    table[key] = Entry{"", true};
    current_size += entrySize(key, "");
}

// verify if key exist (inclusive of tombstones)
// why does "contains" include tombstone as the search and index does not
bool Memtable::contains(const std::string &key) const
{
    return table.count(key) > 0;
}

bool Memtable::isFull() const
{
    return current_size >= max_size;
}

// Memtable with tombstones is still considered not empty?
bool Memtable::isEmpty() const
{
    return table.empty();
}

std::vector<Record> Memtable::flush()
{
    std::vector<Record> records;
    records.reserve(table.size());

    for (const auto &[key, entry] : table)
    {
        records.push_back(Record{
            key,
            entry.tombstone ? "" : entry.value,
            entry.tombstone
        });
    }

    return records;
}

void Memtable::clear()
{
    table.clear();
    current_size = 0;
}

size_t Memtable::sizeBytes() const
{
    return current_size;
}

size_t Memtable::count() const
{
    return table.size();
}

size_t Memtable::entrySize(const std::string &key, const std::string &value) const
{
    return key.size() + value.size() + 64; // why add 64;
}