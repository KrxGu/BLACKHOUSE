#pragma once

#include <cstdint>
#include <cstring>

namespace mbp_reconstructor {

struct alignas(32) Event {
    uint64_t timestamp_ns;
    uint64_t order_id;
    int64_t  price_raw;       // price * 100 to avoid floating point
    uint32_t size;
    uint16_t sequence;
    char     action;          // A,M,C,T,F,R,N
    char     side;            // B,A,N
    char     padding[6];
    
    Event() = default;
    
    Event(uint64_t ts, char act, char sd, int64_t px, uint32_t sz, uint64_t oid)
        : timestamp_ns(ts), order_id(oid), price_raw(px), size(sz), 
          sequence(0), action(act), side(sd) {
        std::memset(padding, 0, sizeof(padding));
    }
    
    bool is_bid() const noexcept { return side == 'B'; }
    bool is_ask() const noexcept { return side == 'A'; }
    bool is_trade() const noexcept { return action == 'T'; }
    bool is_add() const noexcept { return action == 'A'; }
    bool is_modify() const noexcept { return action == 'M'; }
    bool is_cancel() const noexcept { return action == 'C'; }
    bool is_fill() const noexcept { return action == 'F'; }
    bool is_clear() const noexcept { return action == 'R'; }
} __attribute__((packed));

static_assert(sizeof(Event) <= 64, "Event structure should be reasonably sized for cache efficiency");

struct Order {
    uint64_t order_id;
    int64_t  price_raw;
    uint32_t size;
    uint32_t original_size;
    uint64_t timestamp_ns;
    Order*   next;
    Order*   prev;
    
    Order() = default;
    
    Order(uint64_t oid, int64_t px, uint32_t sz, uint64_t ts)
        : order_id(oid), price_raw(px), size(sz), original_size(sz),
          timestamp_ns(ts), next(nullptr), prev(nullptr) {}
          
    void unlink() noexcept {
        if (next) next->prev = prev;
        if (prev) prev->next = next;
        next = prev = nullptr;
    }
};

struct Level {
    int64_t  price_raw;
    uint64_t total_size;
    uint32_t order_count;
    Order*   first_order;
    Order*   last_order;
    
    Level() : price_raw(0), total_size(0), order_count(0), 
              first_order(nullptr), last_order(nullptr) {}
              
    explicit Level(int64_t px) : price_raw(px), total_size(0), order_count(0),
                                 first_order(nullptr), last_order(nullptr) {}
    
    void add_order(Order* order) noexcept {
        if (!first_order) {
            first_order = last_order = order;
        } else {
            last_order->next = order;
            order->prev = last_order;
            last_order = order;
        }
        total_size += order->size;
        ++order_count;
    }
    
    void remove_order(Order* order) noexcept {
        if (order == first_order) first_order = order->next;
        if (order == last_order) last_order = order->prev;
        
        total_size -= order->size;
        --order_count;
        order->unlink();
    }
    
    void modify_order_size(Order* /*order*/, uint32_t old_size, uint32_t new_size) noexcept {
        total_size = total_size - old_size + new_size;
    }
    
    bool empty() const noexcept { return order_count == 0; }
};

struct MBPSnapshot {
    uint64_t timestamp_ns;
    int64_t  bid_px[10];
    uint64_t bid_sz[10];
    int64_t  ask_px[10];
    uint64_t ask_sz[10];
    
    MBPSnapshot() {
        timestamp_ns = 0;
        std::memset(bid_px, 0, sizeof(bid_px));
        std::memset(bid_sz, 0, sizeof(bid_sz));
        std::memset(ask_px, 0, sizeof(ask_px));
        std::memset(ask_sz, 0, sizeof(ask_sz));
    }
    
    bool differs_from(const MBPSnapshot& other) const noexcept {
        return std::memcmp(bid_px, other.bid_px, sizeof(bid_px)) != 0 ||
               std::memcmp(bid_sz, other.bid_sz, sizeof(bid_sz)) != 0 ||
               std::memcmp(ask_px, other.ask_px, sizeof(ask_px)) != 0 ||
               std::memcmp(ask_sz, other.ask_sz, sizeof(ask_sz)) != 0;
    }
};

struct TradeInfo {
    uint64_t timestamp_ns;
    uint64_t trade_id;
    int64_t  price_raw;
    uint32_t size;
    char     side;
    bool     is_aggressor_fill;
    
    TradeInfo() = default;
    
    TradeInfo(uint64_t ts, uint64_t tid, int64_t px, uint32_t sz, char sd)
        : timestamp_ns(ts), trade_id(tid), price_raw(px), size(sz), 
          side(sd), is_aggressor_fill(false) {}
};

struct BidComparator {
    bool operator()(int64_t a, int64_t b) const noexcept {
        return a > b; // higher prices first
    }
};

struct AskComparator {
    bool operator()(int64_t a, int64_t b) const noexcept {
        return a < b; // lower prices first
    }
};

} // namespace mbp_reconstructor 