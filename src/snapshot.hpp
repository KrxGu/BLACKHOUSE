#pragma once

#include "order.hpp"
#include "order_book.hpp"
#include <string>
#include <cstdio>

namespace mbp_reconstructor {

class SnapshotManager {
private:
    MBPSnapshot current_snapshot_;
    MBPSnapshot previous_snapshot_;
    bool has_previous_;
    uint64_t snapshots_generated_;
    uint64_t snapshots_skipped_;
    
public:
    SnapshotManager() : has_previous_(false), snapshots_generated_(0), snapshots_skipped_(0) {}
    
    bool should_generate_snapshot(const OrderBook& book, uint64_t timestamp) {
        book.get_top10_snapshot(current_snapshot_);
        current_snapshot_.timestamp_ns = timestamp;
        
        if (has_previous_ && !current_snapshot_.differs_from(previous_snapshot_)) {
            ++snapshots_skipped_;
            return false;
        }
        
        previous_snapshot_ = current_snapshot_;
        has_previous_ = true;
        ++snapshots_generated_;
        
        return true;
    }
    
    const MBPSnapshot& get_current_snapshot() const {
        return current_snapshot_;
    }
    
    uint64_t get_snapshots_generated() const { return snapshots_generated_; }
    uint64_t get_snapshots_skipped() const { return snapshots_skipped_; }
    double get_compression_ratio() const {
        uint64_t total = snapshots_generated_ + snapshots_skipped_;
        return total > 0 ? static_cast<double>(snapshots_skipped_) / total : 0.0;
    }
};

class MBPFormatter {
private:
    static constexpr size_t BUFFER_SIZE = 1024;
    char buffer_[BUFFER_SIZE];
    
public:
    std::string format_snapshot(const MBPSnapshot& snapshot) {
        std::string result;
        result.reserve(512);
        
        result += std::to_string(snapshot.timestamp_ns);
        
        for (int i = 0; i < 10; ++i) {
            result += ',';
            if (snapshot.bid_px[i] != 0) {
                result += price_to_string(snapshot.bid_px[i]);
            }
            result += ',';
            if (snapshot.bid_sz[i] != 0) {
                result += std::to_string(snapshot.bid_sz[i]);
            }
        }
        
        for (int i = 0; i < 10; ++i) {
            result += ',';
            if (snapshot.ask_px[i] != 0) {
                result += price_to_string(snapshot.ask_px[i]);
            }
            result += ',';
            if (snapshot.ask_sz[i] != 0) {
                result += std::to_string(snapshot.ask_sz[i]);
            }
        }
        
        result += '\n';
        return result;
    }
    
    size_t format_snapshot_fast(const MBPSnapshot& snapshot, char* output, size_t max_len) {
        char* ptr = output;
        char* end = output + max_len;
        
        ptr += snprintf(ptr, end - ptr, "%llu", (unsigned long long)snapshot.timestamp_ns);
        
        for (int i = 0; i < 10; ++i) {
            if (snapshot.bid_px[i] != 0) {
                ptr += snprintf(ptr, end - ptr, ",%.2f,%llu", 
                               snapshot.bid_px[i] / 100.0, (unsigned long long)snapshot.bid_sz[i]);
            } else {
                ptr += snprintf(ptr, end - ptr, ",,");
            }
        }
        
        for (int i = 0; i < 10; ++i) {
            if (snapshot.ask_px[i] != 0) {
                ptr += snprintf(ptr, end - ptr, ",%.2f,%llu", 
                               snapshot.ask_px[i] / 100.0, (unsigned long long)snapshot.ask_sz[i]);
            } else {
                ptr += snprintf(ptr, end - ptr, ",,");
            }
        }
        
        *ptr++ = '\n';
        return ptr - output;
    }
    
private:
    std::string price_to_string(int64_t price_raw) {
        int64_t whole = price_raw / 100;
        int64_t frac = price_raw % 100;
        
        if (frac == 0) {
            return std::to_string(whole);
        } else {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%lld.%02lld", (long long)whole, (long long)frac);
            return std::string(buffer);
        }
    }
};

class CSVHeader {
public:
    static std::string generate_mbp_header() {
        std::string header = "ts_event";
        
        for (int i = 0; i < 10; ++i) {
            header += ",bid_px_" + format_level_index(i);
            header += ",bid_sz_" + format_level_index(i);
        }
        
        for (int i = 0; i < 10; ++i) {
            header += ",ask_px_" + format_level_index(i);
            header += ",ask_sz_" + format_level_index(i);
        }
        
        header += '\n';
        return header;
    }
    
private:
    static std::string format_level_index(int index) {
        if (index < 10) {
            return "0" + std::to_string(index);
        }
        return std::to_string(index);
    }
};

class SnapshotProcessor {
private:
    SnapshotManager manager_;
    MBPFormatter formatter_;
    std::string output_buffer_;
    
    uint64_t total_events_processed_;
    uint64_t snapshots_written_;
    
public:
    SnapshotProcessor() : total_events_processed_(0), snapshots_written_(0) {
        output_buffer_.reserve(8192);
    }
    
    std::string process_event(const OrderBook& book, uint64_t timestamp) {
        ++total_events_processed_;
        
        if (manager_.should_generate_snapshot(book, timestamp)) {
            ++snapshots_written_;
            return formatter_.format_snapshot(manager_.get_current_snapshot());
        }
        
        return std::string{};
    }
    
    void process_events_batch(const OrderBook& book, 
                             const std::vector<uint64_t>& timestamps,
                             std::vector<std::string>& output) {
        output.clear();
        output.reserve(timestamps.size());
        
        for (uint64_t timestamp : timestamps) {
            std::string snapshot = process_event(book, timestamp);
            if (!snapshot.empty()) {
                output.push_back(std::move(snapshot));
            }
        }
    }
    
    // Performance statistics
    void print_statistics() const {
        printf("Snapshot Statistics:\n");
        printf("  Events processed: %llu\n", (unsigned long long)total_events_processed_);
        printf("  Snapshots written: %llu\n", (unsigned long long)snapshots_written_);
        printf("  Snapshots generated: %llu\n", (unsigned long long)manager_.get_snapshots_generated());
        printf("  Snapshots skipped: %llu\n", (unsigned long long)manager_.get_snapshots_skipped());
        printf("  Compression ratio: %.2f%%\n", manager_.get_compression_ratio() * 100.0);
        
        if (total_events_processed_ > 0) {
            printf("  Output rate: %.2f%%\n", 
                   static_cast<double>(snapshots_written_) / total_events_processed_ * 100.0);
        }
    }
};

} // namespace mbp_reconstructor 