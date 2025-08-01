#pragma once

#include "order.hpp"
#include "../include/robin_hood.h"
#include <map>
#include <memory>
#include <array>
#include <algorithm>
#include <vector>

namespace mbp_reconstructor {

class OrderPool {
private:
    static constexpr size_t POOL_SIZE = 50000;
    std::array<Order, POOL_SIZE> pool_;
    std::vector<Order*> free_list_;
    
public:
    OrderPool() {
        free_list_.reserve(POOL_SIZE);
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            free_list_.push_back(&pool_[i]);
        }
    }
    
    Order* allocate() {
        if (!free_list_.empty()) {
            Order* order = free_list_.back();
            free_list_.pop_back();
            return order;
        }
        
        return new Order();
    }
    
    void deallocate(Order* order) {
        if (order >= &pool_[0] && order < &pool_[POOL_SIZE]) {
            *order = Order{};
            free_list_.push_back(order);
        } else {
            delete order;
        }
    }
};

class OrderBook {
private:
    std::map<int64_t, Level, BidComparator> bid_levels_;
    std::map<int64_t, Level, AskComparator> ask_levels_;
    
    robin_hood::unordered_flat_map<uint64_t, Order*> order_map_;
    
    OrderPool order_pool_;
    
    mutable std::array<int64_t, 10> cached_bid_prices_;
    mutable std::array<uint64_t, 10> cached_bid_sizes_;
    mutable std::array<int64_t, 10> cached_ask_prices_;
    mutable std::array<uint64_t, 10> cached_ask_sizes_;
    mutable bool cache_valid_;
    
    mutable uint64_t total_orders_processed_;
    mutable uint64_t price_levels_created_;
    
public:
    OrderBook() : cache_valid_(false), total_orders_processed_(0), 
                  price_levels_created_(0) {
        order_map_.reserve(10000);
        
        cached_bid_prices_.fill(0);
        cached_bid_sizes_.fill(0);
        cached_ask_prices_.fill(0);
        cached_ask_sizes_.fill(0);
    }
    
    ~OrderBook() {
        clear();
    }
    
    bool add_order(uint64_t order_id, int64_t price, uint32_t size, char side, uint64_t timestamp) {
        if (order_map_.count(order_id)) {
            return false;
        }
        
        Order* order = order_pool_.allocate();
        *order = Order(order_id, price, size, timestamp);
        
        bool success = false;
        if (side == 'B') {
            success = add_to_side(order, bid_levels_);
        } else if (side == 'A') {
            success = add_to_side(order, ask_levels_);
        }
        
        if (success) {
            order_map_[order_id] = order;
            cache_valid_ = false;
            ++total_orders_processed_;
        } else {
            order_pool_.deallocate(order);
        }
        
        return success;
    }
    
    bool modify_order(uint64_t order_id, int64_t new_price, uint32_t new_size) {
        auto it = order_map_.find(order_id);
        if (it == order_map_.end()) {
            return false;
        }
        
        Order* order = it->second;
        int64_t old_price = order->price_raw;
        uint32_t old_size = order->size;
        
        if (old_price != new_price) {
            char side = determine_side(order);
            remove_order_from_level(order, side);
            
            order->price_raw = new_price;
            order->size = new_size;
            
            bool success = false;
            if (side == 'B') {
                success = add_to_side(order, bid_levels_);
            } else {
                success = add_to_side(order, ask_levels_);
            }
            
            if (!success) {
                order_map_.erase(it);
                order_pool_.deallocate(order);
                return false;
            }
        } else {
            char side = determine_side(order);
            if (side == 'B') {
                auto level_it = bid_levels_.find(old_price);
                if (level_it != bid_levels_.end()) {
                    level_it->second.modify_order_size(order, old_size, new_size);
                    order->size = new_size;
                }
            } else {
                auto level_it = ask_levels_.find(old_price);
                if (level_it != ask_levels_.end()) {
                    level_it->second.modify_order_size(order, old_size, new_size);
                    order->size = new_size;
                }
            }
        }
        
        cache_valid_ = false;
        return true;
    }
    
    bool cancel_order(uint64_t order_id) {
        auto it = order_map_.find(order_id);
        if (it == order_map_.end()) {
            return false;
        }
        
        Order* order = it->second;
        char side = determine_side(order);
        
        remove_order_from_level(order, side);
        order_map_.erase(it);
        order_pool_.deallocate(order);
        
        cache_valid_ = false;
        return true;
    }
    
    bool execute_trade(int64_t price, uint32_t size, char aggressor_side) {
        char passive_side = (aggressor_side == 'B') ? 'A' : 'B';
        
        if (passive_side == 'B') {
            return execute_trade_on_side(price, size, bid_levels_);
        } else {
            return execute_trade_on_side(price, size, ask_levels_);
        }
    }

private:
    template<typename LevelMap>
    bool execute_trade_on_side(int64_t price, uint32_t size, LevelMap& levels) {
        auto level_it = levels.find(price);
        if (level_it == levels.end()) {
            return false;
        }
        
        Level& level = level_it->second;
        uint32_t remaining_size = size;
        
        while (remaining_size > 0 && level.first_order != nullptr) {
            Order* order = level.first_order;
            
            if (order->size <= remaining_size) {
                remaining_size -= order->size;
                
                order_map_.erase(order->order_id);
                level.remove_order(order);
                order_pool_.deallocate(order);
            } else {
                uint32_t old_size = order->size;
                order->size -= remaining_size;
                level.modify_order_size(order, old_size, order->size);
                remaining_size = 0;
            }
        }
        
        if (level.empty()) {
            levels.erase(level_it);
        }
        
        cache_valid_ = false;
        return true;
    }

public:
     
    void clear() {
        for (auto& [order_id, order] : order_map_) {
            order_pool_.deallocate(order);
        }
        
        order_map_.clear();
        bid_levels_.clear();
        ask_levels_.clear();
        cache_valid_ = false;
    }
    
    void get_top10_snapshot(MBPSnapshot& snapshot) const {
        if (!cache_valid_) {
            update_cache();
        }
        
        std::copy(cached_bid_prices_.begin(), cached_bid_prices_.end(), snapshot.bid_px);
        std::copy(cached_bid_sizes_.begin(), cached_bid_sizes_.end(), snapshot.bid_sz);
        std::copy(cached_ask_prices_.begin(), cached_ask_prices_.end(), snapshot.ask_px);
        std::copy(cached_ask_sizes_.begin(), cached_ask_sizes_.end(), snapshot.ask_sz);
    }
    
    std::pair<int64_t, uint64_t> get_best_bid() const {
        if (bid_levels_.empty()) return {0, 0};
        auto it = bid_levels_.begin();
        return {it->first, it->second.total_size};
    }
    
    std::pair<int64_t, uint64_t> get_best_ask() const {
        if (ask_levels_.empty()) return {0, 0};
        auto it = ask_levels_.begin();
        return {it->first, it->second.total_size};
    }
    
    uint64_t get_total_orders() const { return total_orders_processed_; }
    size_t get_active_orders() const { return order_map_.size(); }
    size_t get_price_levels() const { return bid_levels_.size() + ask_levels_.size(); }
    
private:
    template<typename LevelMap>
    bool add_to_side(Order* order, LevelMap& levels) {
        auto& level = levels[order->price_raw];
        
        if (level.empty()) {
            level.price_raw = order->price_raw;
            ++price_levels_created_;
        }
        
        level.add_order(order);
        return true;
    }
    
    char determine_side(const Order* order) const {
        auto bid_it = bid_levels_.find(order->price_raw);
        if (bid_it != bid_levels_.end()) {
            for (Order* o = bid_it->second.first_order; o != nullptr; o = o->next) {
                if (o == order) return 'B';
            }
        }
        
        return 'A';
    }
    
    void remove_order_from_level(Order* order, char side) {
        if (side == 'B') {
            auto level_it = bid_levels_.find(order->price_raw);
            if (level_it != bid_levels_.end()) {
                level_it->second.remove_order(order);
                if (level_it->second.empty()) {
                    bid_levels_.erase(level_it);
                }
            }
        } else {
            auto level_it = ask_levels_.find(order->price_raw);
            if (level_it != ask_levels_.end()) {
                level_it->second.remove_order(order);
                if (level_it->second.empty()) {
                    ask_levels_.erase(level_it);
                }
            }
        }
    }
    
    void update_cache() const {
        cached_bid_prices_.fill(0);
        cached_bid_sizes_.fill(0);
        cached_ask_prices_.fill(0);
        cached_ask_sizes_.fill(0);
        
        size_t bid_idx = 0;
        for (auto it = bid_levels_.begin(); it != bid_levels_.end() && bid_idx < 10; ++it, ++bid_idx) {
            cached_bid_prices_[bid_idx] = it->first;
            cached_bid_sizes_[bid_idx] = it->second.total_size;
        }
        
        size_t ask_idx = 0;
        for (auto it = ask_levels_.begin(); it != ask_levels_.end() && ask_idx < 10; ++it, ++ask_idx) {
            cached_ask_prices_[ask_idx] = it->first;
            cached_ask_sizes_[ask_idx] = it->second.total_size;
        }
        
        cache_valid_ = true;
    }
};

} // namespace mbp_reconstructor 