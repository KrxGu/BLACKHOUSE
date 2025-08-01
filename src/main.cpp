#include "csv_parser.hpp"
#include "order_book.hpp"
#include "action_engine.hpp"
#include "snapshot.hpp"
#include <iostream>
#include <chrono>
#include <memory>

using namespace mbp_reconstructor;

class PerformanceTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    PerformanceTimer() {
        start();
    }
    
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    double elapsed_seconds() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        return duration.count() / 1000000.0;
    }
    
    void print_elapsed(const std::string& label) const {
        std::cerr << label << ": " << elapsed_seconds() << " seconds" << std::endl;
    }
};

class MBPReconstructor {
private:
    std::unique_ptr<OrderBook> order_book_;
    std::unique_ptr<ActionEngine> action_engine_;
    std::unique_ptr<SnapshotProcessor> snapshot_processor_;
    
    uint64_t events_processed_;
    uint64_t snapshots_emitted_;
    
public:
    MBPReconstructor() : events_processed_(0), snapshots_emitted_(0) {
        order_book_ = std::make_unique<OrderBook>();
        action_engine_ = std::make_unique<ActionEngine>(*order_book_);
        snapshot_processor_ = std::make_unique<SnapshotProcessor>();
    }
    
    bool reconstruct(const char* input_filename) {
        try {
            PerformanceTimer timer;
            
            FastCSVParser parser(input_filename);
            
            std::cout << CSVHeader::generate_mbp_header();
            
            Event event;
            while (parser.parse_next_event(event)) {
                ++events_processed_;
                
                bool should_snapshot = action_engine_->process_event(event);
                
                if (should_snapshot) {
                    std::string snapshot_line = snapshot_processor_->process_event(
                        *order_book_, event.timestamp_ns);
                    
                    if (!snapshot_line.empty()) {
                        std::cout << snapshot_line;
                        ++snapshots_emitted_;
                    }
                }
                
                if (events_processed_ % 100000 == 0) {
                    std::cerr << "Processed " << events_processed_ << " events..." << std::endl;
                }
            }
            
            timer.print_elapsed("Total processing time");
            print_statistics();
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return false;
        }
    }
    
private:
    void print_statistics() const {
        std::cerr << "\n=== Performance Statistics ===" << std::endl;
        std::cerr << "Events processed: " << events_processed_ << std::endl;
        std::cerr << "Snapshots emitted: " << snapshots_emitted_ << std::endl;
        
        if (events_processed_ > 0) {
                    std::cerr << "Events per snapshot: " << 
            static_cast<double>(events_processed_) / snapshots_emitted_ << std::endl;
        std::cerr << "Compression ratio: " << 
            (1.0 - static_cast<double>(snapshots_emitted_) / events_processed_) * 100.0 
            << "%" << std::endl;
    }
    
    std::cerr << "Active orders: " << order_book_->get_active_orders() << std::endl;
    std::cerr << "Price levels: " << order_book_->get_price_levels() << std::endl;
    std::cerr << "Total orders processed: " << order_book_->get_total_orders() << std::endl;
    
    std::cerr << "Actions processed: " << action_engine_->get_actions_processed() << std::endl;
    std::cerr << "Trades aggregated: " << action_engine_->get_trades_aggregated() << std::endl;
    std::cerr << "Errors encountered: " << action_engine_->get_errors_encountered() << std::endl;
    
    snapshot_processor_->print_statistics();
    }
};

class DebugReconstructor : public MBPReconstructor {
private:
    bool verbose_mode_;
    uint64_t max_events_;
    
public:
    DebugReconstructor(bool verbose = false, uint64_t max_events = UINT64_MAX) 
        : verbose_mode_(verbose), max_events_(max_events) {}
    
    bool reconstruct_debug(const char* input_filename) {
        try {
            FastCSVParser parser(input_filename);
            
            std::cout << CSVHeader::generate_mbp_header();
            
            Event event;
            uint64_t event_count = 0;
            
            while (parser.parse_next_event(event) && event_count < max_events_) {
                ++event_count;
                
                if (verbose_mode_) {
                    std::cerr << "Event " << event_count << ": " 
                             << event.action << " " << event.side 
                             << " @" << event.price_raw/100.0 
                             << " size=" << event.size 
                             << " oid=" << event.order_id << std::endl;
                }
            }
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Debug Error: " << e.what() << std::endl;
            return false;
        }
    }
};

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <input_mbo_file.csv>" << std::endl;
    std::cerr << "\nOptions:" << std::endl;
    std::cerr << "  --debug           Enable debug mode with verbose output" << std::endl;
    std::cerr << "  --max-events N    Process only first N events (debug mode)" << std::endl;
    std::cerr << "\nExample:" << std::endl;
    std::cerr << "  " << program_name << " data/mbo.csv > output/mbp.csv" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    bool debug_mode = false;
    uint64_t max_events = UINT64_MAX;
    const char* input_file = nullptr;
    
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") {
            debug_mode = true;
        } else if (std::string(argv[i]) == "--max-events" && i + 1 < argc) {
            max_events = std::stoull(argv[++i]);
        } else {
            input_file = argv[i];
        }
    }
    
    if (!input_file) {
        std::cerr << "Error: No input file specified" << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    std::cerr << "MBP Reconstructor v1.0 - High Performance Order Book Reconstruction" << std::endl;
    std::cerr << "Input file: " << input_file << std::endl;
    
    bool success = false;
    
    if (debug_mode) {
        std::cerr << "Running in debug mode..." << std::endl;
        DebugReconstructor debug_reconstructor(true, max_events);
        success = debug_reconstructor.reconstruct_debug(input_file);
    } else {
        MBPReconstructor reconstructor;
        success = reconstructor.reconstruct(input_file);
    }
    
    if (!success) {
        std::cerr << "Reconstruction failed!" << std::endl;
        return 1;
    }
    
    std::cerr << "Reconstruction completed successfully." << std::endl;
    return 0;
} 