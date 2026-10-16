#include "segment_manager.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <map>

SegmentManager::SegmentManager(const std::string &directory) : data_dir(directory)
{
    // load segments
    if (!std::filesystem::exists(data_dir))
    {
        // create_directories - can create directories if they don't already exist 
        // create_directory - requires the directory to already exist
        std::filesystem::create_directories(data_dir); 
    }

    loadAllSegments();

    // if segment empty, create at least one
    if (segments.empty())
    {
        addNewSegment();
    }
}

// Disable background thread
SegmentManager::~SegmentManager()
{
    should_stop = true;

    // Prevent SegmentManager from destroying if background thread still active
    if (compaction_thread.joinable())
    {
        compaction_thread.join();
    }
}

Segment *SegmentManager::getActiveSegment()
{
    return segments.back().get(); // specifically designed to return a raw pointer
}

std::pair<size_t, size_t> SegmentManager::write(const Record &record)
{
    std::lock_guard<std::mutex> lock(segments_mutex);

    if (getActiveSegment()->isFull())
    {
        std::cout << "[SegmentManager] Segment full, rotating to new segment \n";
        addNewSegment();
    }

    size_t seg_idx = segments.size() - 1;
    size_t offset = getActiveSegment()->write(record);

    return {seg_idx, offset};
}

std::optional<Record> SegmentManager::read(size_t segment_idx, size_t offset)
{
    std::lock_guard<std::mutex> lock(segments_mutex);

    if (segment_idx >= segments.size())
    {
        return std::nullopt;
    }

    return segments[segment_idx]->read(offset);
}

std::vector<std::tuple<size_t, size_t, Record>> SegmentManager::readAll()
{
    std::lock_guard<std::mutex> lock(segments_mutex);

    std::vector<std::tuple<size_t, size_t, Record>> all_records;

    // loop through each segment
    // for each segment, read the records on the index
    for (size_t i = 0; i < segments.size(); i++)
    {
        auto records = segments[i]->readAll();
        size_t offset = 0;

        for (const auto &record : records)
        {
            all_records.emplace_back(i, offset, record);
            offset += 4 + 4 + 1 + record.key.size() + record.value.size();
        }
    }

    return all_records;
}

void SegmentManager::compact(Index &index)
{
    std::lock_guard<std::mutex> lock(segments_mutex);

    std::cout << "[SegmentManager] Starting compaction of " << segments.size() << " segments..." << std::endl;

    std::map<std::string, std::string> recent_records;

    // Build merged indexes - Keys in sorted order
    for (const auto &segment : segments)
    {
        for (const auto &record : segment->readAll())
        {
            if (record.tombstone)
            {
                recent_records.erase(record.key);
            }
            else
            {
                recent_records[record.key] = record.value;
            }
        }
    }

    // Write all items to a new segment
    // TODO: if there are no tombstone records this could grow to be a larger file
    std::string compacted_path = segmentPath(0) + ".compacted";
    auto compacted_segment = std::make_unique<Segment>(compacted_path);

    std::unordered_map<std::string, size_t> recent_offsets;
    size_t current_offset = 0;

    for (const auto &[key, value] : recent_records)
    {
        auto record = Record{key, value, false};
        compacted_segment->write(record);

        recent_offsets[key] = current_offset;
        current_offset += 4 + 4 + 1 + key.size() + value.size();
    }

    for (const auto &segment : segments)
    {
        segment->deleteFile();
    }

    // Clean up segments vector
    segments.clear();

    // Rename compacted file
    std::string segment_path = segmentPath(0);
    std::filesystem::rename(compacted_path, segment_path);

    segments.push_back(std::make_unique<Segment>(segment_path));

    // Clean old indexes
    for (const auto &key : index.keys())
    {
        index.remove(key);
    }

    // Populate indexes
    for (const auto &[key, off] : recent_offsets)
    {
        index.put(key, off);
    }

    std::cout << "[SegmentManager] Compaction complete! " << index.size() << " records retained" << std::endl;
}

// This runs in the background while program is not active??
void SegmentManager::compactionLoop(Index *index)
{
    while (!should_stop)
    {
        // Sleep for 30s (1s interval) as long as should_stop not enabled
        for (int i = 0; i < 30 && !should_stop; i++)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (should_stop)
            break;

        // Check if we have enough segments to warrant compaction
        size_t count;
        // By putting lock in this block we keep it scoped since we don't need it outside this block
        // TODO: what if we didn't add this to a block
        {
            std::lock_guard<std::mutex> lock(segments_mutex);
            count = segments.size(); 
        }

        if (count >= COMPACTION_THRESHOLD)
        {
            std::cout << "[SegmentManager] Auto-compacting "
                      << count << " segments...\n";
            compact(*index); // there is a lock in here as well
        }
    }
}

std::string SegmentManager::segmentPath(size_t idx) const
{
    std::ostringstream oss;
    oss << data_dir << "/segment_" << std::setw(4) << std::setfill('0') << idx << ".seg"; // what does this do
    return oss.str();
}

void SegmentManager::addNewSegment()
{
    size_t idx = segments.size();

    segments.push_back(std::make_unique<Segment>(segmentPath(idx)));

    std::cout
        << "[SegmentManager] Created segment: " << segmentPath(idx) << std::endl;
}

void SegmentManager::loadAllSegments()
{
    // load all files with .seg extension
    std::vector<std::string> paths;

    for (const auto &current_file : std::filesystem::directory_iterator(data_dir))
    {
        if (current_file.path().extension() == ".seg")
        {
            paths.push_back(current_file.path().string());
        }
    }

    // iterate in descending order: std::sort(paths.begin(), paths.end(), std::greater<std::string>())
    std::sort(paths.begin(), paths.end());

    for (const auto &path : paths)
    {
        segments.push_back(std::make_unique<Segment>(path)); // keeps segment constant in one file location while resizing pointer address
    }
}

size_t SegmentManager::segmentCount() const
{
    std::lock_guard<std::mutex> lock(segments_mutex); // mutex unlocks after function goes out of scope
    return segments.size(); 
}