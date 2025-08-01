#pragma once

#include "order.hpp"
#include "order_book.hpp"
#include <unordered_map>
#include <optional>

namespace mbp_reconstructor {

enum class TradeState {
    IDLE,
    TRADE_RECEIVED,
    FILL_RECEIVED
};

class ActionEngine {
private:
    OrderBook& order_book_;
    
    TradeState trade_state_;
    std::optional<TradeInfo> pending_trade_;
    uint64_t last_trade_id_;
    
    uint64_t actions_processed_;
    uint64_t trades_aggregated_;
    uint64_t errors_encountered_;
    
    bool first_clear_seen_;
    
public:
    explicit ActionEngine(OrderBook& book) 
        : order_book_(book), trade_state_(TradeState::IDLE), 
          last_trade_id_(0), actions_processed_(0), 
          trades_aggregated_(0), errors_encountered_(0),
          first_clear_seen_(false) {}
    
    bool process_event(const Event& event) {
        ++actions_processed_;
        
        switch (event.action) {
            case 'A':
                return handle_add(event);
            case 'M':
                return handle_modify(event);
            case 'C':
                return handle_cancel(event);
            case 'T':
                return handle_trade(event);
            case 'F':
                return handle_fill(event);
            case 'R':
                return handle_clear(event);
            case 'N':
                return true;
            default:
                ++errors_encountered_;
                return false;
        }
    }
    
    uint64_t get_actions_processed() const { return actions_processed_; }
    uint64_t get_trades_aggregated() const { return trades_aggregated_; }
    uint64_t get_errors_encountered() const { return errors_encountered_; }
    
private:
    bool handle_add(const Event& event) {
        if (event.side == 'N') {
            return false;
        }
        
        bool success = order_book_.add_order(
            event.order_id, 
            event.price_raw, 
            event.size, 
            event.side, 
            event.timestamp_ns
        );
        
        if (!success) {
            ++errors_encountered_;
        }
        
        return success;
    }
    
    bool handle_modify(const Event& event) {
        if (event.side == 'N') {
            return false;
        }
        
        bool success = order_book_.modify_order(
            event.order_id,
            event.price_raw,
            event.size
        );
        
        if (!success) {
            ++errors_encountered_;
        }
        
        return success;
    }
    
    bool handle_cancel(const Event& event) {
        if (trade_state_ == TradeState::FILL_RECEIVED) {
            return complete_trade_sequence(event);
        } else {
            bool success = order_book_.cancel_order(event.order_id);
            if (!success) {
                ++errors_encountered_;
            }
            return success;
        }
    }
    
    bool handle_trade(const Event& event) {
        trade_state_ = TradeState::TRADE_RECEIVED;
        pending_trade_ = TradeInfo(
            event.timestamp_ns,
            event.order_id,
            event.price_raw,
            event.size,
            event.side
        );
        last_trade_id_ = event.order_id;
        
        return false;
    }
    
    bool handle_fill(const Event& event) {
        if (trade_state_ != TradeState::TRADE_RECEIVED) {
            ++errors_encountered_;
            return false;
        }
        
        if (pending_trade_ && event.order_id == last_trade_id_) {
            trade_state_ = TradeState::FILL_RECEIVED;
            pending_trade_->is_aggressor_fill = true;
        } else {
            trade_state_ = TradeState::IDLE;
            pending_trade_.reset();
            ++errors_encountered_;
        }
        
        return false;
    }
    
    bool handle_clear(const Event& /*event*/) {
        if (!first_clear_seen_) {
            first_clear_seen_ = true;
            return false;
        }
        
        order_book_.clear();
        
        trade_state_ = TradeState::IDLE;
        pending_trade_.reset();
        
        return true;
    }
    
    bool complete_trade_sequence(const Event& /*cancel_event*/) {
        if (!pending_trade_ || trade_state_ != TradeState::FILL_RECEIVED) {
            ++errors_encountered_;
            trade_state_ = TradeState::IDLE;
            pending_trade_.reset();
            return false;
        }
        
        bool success = order_book_.execute_trade(
            pending_trade_->price_raw,
            pending_trade_->size,
            pending_trade_->side
        );
        
        if (!success) {
            ++errors_encountered_;
        } else {
            ++trades_aggregated_;
        }
        
        trade_state_ = TradeState::IDLE;
        pending_trade_.reset();
        
        return success;
    }
    
    bool validate_trade_sequence(const Event& event) const {
        if (!pending_trade_) {
            return false;
        }
        
        return event.order_id == last_trade_id_ &&
               event.price_raw == pending_trade_->price_raw;
    }
};

class VerboseActionEngine : public ActionEngine {
private:
    struct ActionStats {
        uint64_t adds = 0;
        uint64_t modifies = 0;
        uint64_t cancels = 0;
        uint64_t trades = 0;
        uint64_t fills = 0;
        uint64_t clears = 0;
        uint64_t noops = 0;
    };
    
    ActionStats stats_;
    
public:
    explicit VerboseActionEngine(OrderBook& book) : ActionEngine(book) {}
    
    bool process_event_verbose(const Event& event) {
        switch (event.action) {
            case 'A': ++stats_.adds; break;
            case 'M': ++stats_.modifies; break;
            case 'C': ++stats_.cancels; break;
            case 'T': ++stats_.trades; break;
            case 'F': ++stats_.fills; break;
            case 'R': ++stats_.clears; break;
            case 'N': ++stats_.noops; break;
        }
        
        return ActionEngine::process_event(event);
    }
    
    void print_statistics() const {
        printf("Action Statistics:\n");
        printf("  Adds: %llu\n", (unsigned long long)stats_.adds);
        printf("  Modifies: %llu\n", (unsigned long long)stats_.modifies);
        printf("  Cancels: %llu\n", (unsigned long long)stats_.cancels);
        printf("  Trades: %llu\n", (unsigned long long)stats_.trades);
        printf("  Fills: %llu\n", (unsigned long long)stats_.fills);
        printf("  Clears: %llu\n", (unsigned long long)stats_.clears);
        printf("  No-ops: %llu\n", (unsigned long long)stats_.noops);
        printf("  Total processed: %llu\n", (unsigned long long)get_actions_processed());
        printf("  Trades aggregated: %llu\n", (unsigned long long)get_trades_aggregated());
        printf("  Errors: %llu\n", (unsigned long long)get_errors_encountered());
    }
};

} // namespace mbp_reconstructor 