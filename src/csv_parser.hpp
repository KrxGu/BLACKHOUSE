#pragma once

#include "order.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace mbp_reconstructor {

class FastCSVParser {
private:
    int fd_;
    char* data_;
    size_t file_size_;
    char* current_;
    char* end_;
    bool first_line_skipped_;
    
public:
    explicit FastCSVParser(const char* filename) 
        : fd_(-1), data_(nullptr), file_size_(0), current_(nullptr), 
          end_(nullptr), first_line_skipped_(false) {
        
        fd_ = open(filename, O_RDONLY);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file");
        }
        
        struct stat sb;
        if (fstat(fd_, &sb) == -1) {
            close(fd_);
            throw std::runtime_error("Failed to get file size");
        }
        file_size_ = sb.st_size;
        
        data_ = static_cast<char*>(mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE, fd_, 0));
        if (data_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap file");
        }
        
        madvise(data_, file_size_, MADV_SEQUENTIAL);
        
        current_ = data_;
        end_ = data_ + file_size_;
    }
    
    ~FastCSVParser() {
        if (data_ != nullptr && data_ != MAP_FAILED) {
            munmap(data_, file_size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }
    
    FastCSVParser(const FastCSVParser&) = delete;
    FastCSVParser& operator=(const FastCSVParser&) = delete;
    
    bool parse_next_event(Event& event) {
        if (current_ >= end_) {
            return false;
        }
        
        if (!first_line_skipped_) {
            skip_to_next_line();
            first_line_skipped_ = true;
            if (current_ >= end_) return false;
        }
        
        // ts_event,action,side,price,size,order_id,flags,ts_recv,ts_in_delta,sequence
        event.timestamp_ns = parse_uint64();
        expect_char(',');
        
        event.action = *current_;
        advance_char();
        expect_char(',');
        
        event.side = *current_;
        advance_char();
        expect_char(',');
        
        event.price_raw = parse_price();
        expect_char(',');
        
        event.size = parse_uint32();
        expect_char(',');
        
        event.order_id = parse_uint64();
        
        skip_to_next_line();
        
        return true;
    }
    
private:
    uint64_t parse_uint64() {
        uint64_t result = 0;
        while (current_ < end_ && *current_ >= '0' && *current_ <= '9') {
            result = result * 10 + (*current_ - '0');
            ++current_;
        }
        return result;
    }
    
    uint32_t parse_uint32() {
        uint32_t result = 0;
        while (current_ < end_ && *current_ >= '0' && *current_ <= '9') {
            result = result * 10 + (*current_ - '0');
            ++current_;
        }
        return result;
    }
    
    int64_t parse_price() {
        int64_t result = 0;
        bool negative = false;
        
        if (*current_ == '-') {
            negative = true;
            ++current_;
        }
        
        while (current_ < end_ && *current_ >= '0' && *current_ <= '9') {
            result = result * 10 + (*current_ - '0');
            ++current_;
        }
        
        if (current_ < end_ && *current_ == '.') {
            ++current_;
            result *= 100;
            
            if (current_ < end_ && *current_ >= '0' && *current_ <= '9') {
                result += (*current_ - '0') * 10;
                ++current_;
            }
            
            if (current_ < end_ && *current_ >= '0' && *current_ <= '9') {
                result += (*current_ - '0');
                ++current_;
            }
        } else {
            result *= 100;
        }
        
        return negative ? -result : result;
    }
    
    void expect_char(char expected) {
        if (current_ >= end_ || *current_ != expected) {
            return;
        }
        ++current_;
    }
    
    void advance_char() {
        if (current_ < end_) {
            ++current_;
        }
    }
    
    void skip_to_next_line() {
        while (current_ < end_ && *current_ != '\n') {
            ++current_;
        }
        if (current_ < end_ && *current_ == '\n') {
            ++current_;
        }
    }
};

#ifdef __AVX2__
#include <immintrin.h>

class SIMDCSVParser {
    // SIMD implementation for vectorized parsing
};
#endif

} // namespace mbp_reconstructor 