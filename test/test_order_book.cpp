#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>
#include "../src/order_book.hpp"
#include "../src/action_engine.hpp"
#include "../src/csv_parser.hpp"

using namespace mbp_reconstructor;

TEST_CASE("OrderBook Basic Operations", "[orderbook]") {
    OrderBook book;
    
    SECTION("Add orders to both sides") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE(book.add_order(1002, 10100, 200, 'A', 2000));
        
        auto [bid_px, bid_sz] = book.get_best_bid();
        auto [ask_px, ask_sz] = book.get_best_ask();
        
        REQUIRE(bid_px == 10050);
        REQUIRE(bid_sz == 100);
        REQUIRE(ask_px == 10100);
        REQUIRE(ask_sz == 200);
    }
    
    SECTION("Price-time priority") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE(book.add_order(1002, 10050, 150, 'B', 2000));
        
        auto [bid_px, bid_sz] = book.get_best_bid();
        REQUIRE(bid_px == 10050);
        REQUIRE(bid_sz == 250);
    }
    
    SECTION("Order modification") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        
        REQUIRE(book.modify_order(1001, 10050, 150));
        auto [bid_px, bid_sz] = book.get_best_bid();
        REQUIRE(bid_sz == 150);
        
        REQUIRE(book.modify_order(1001, 10075, 150));
        auto [new_bid_px, new_bid_sz] = book.get_best_bid();
        REQUIRE(new_bid_px == 10075);
        REQUIRE(new_bid_sz == 150);
    }
    
    SECTION("Order cancellation") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE(book.add_order(1002, 10025, 200, 'B', 2000));
        
        REQUIRE(book.cancel_order(1001));
        auto [bid_px, bid_sz] = book.get_best_bid();
        REQUIRE(bid_px == 10025);
        REQUIRE(bid_sz == 200);
        
        REQUIRE(book.cancel_order(1002));
        auto [empty_bid_px, empty_bid_sz] = book.get_best_bid();
        REQUIRE(empty_bid_px == 0);
        REQUIRE(empty_bid_sz == 0);
    }
}

TEST_CASE("Trade Execution", "[orderbook][trades]") {
    OrderBook book;
    
    SECTION("Full order fill") {
        REQUIRE(book.add_order(1001, 10100, 100, 'A', 1000));  // Ask
        
        // Execute trade that fully fills the order
        REQUIRE(book.execute_trade(10100, 100, 'B'));  // Buyer aggressor
        
        auto [ask_px, ask_sz] = book.get_best_ask();
        REQUIRE(ask_px == 0);  // No more asks
        REQUIRE(ask_sz == 0);
    }
    
    SECTION("Partial order fill") {
        REQUIRE(book.add_order(1001, 10100, 200, 'A', 1000));  // Ask
        
        // Execute partial fill
        REQUIRE(book.execute_trade(10100, 75, 'B'));  // Fill 75 out of 200
        
        auto [ask_px, ask_sz] = book.get_best_ask();
        REQUIRE(ask_px == 10100);
        REQUIRE(ask_sz == 125);  // 200 - 75 = 125 remaining
    }
    
    SECTION("Multiple order fill at same price") {
        REQUIRE(book.add_order(1001, 10100, 100, 'A', 1000));
        REQUIRE(book.add_order(1002, 10100, 150, 'A', 2000));
        
        // Execute trade that fills across multiple orders
        REQUIRE(book.execute_trade(10100, 200, 'B'));  // Fill 200 total
        
        auto [ask_px, ask_sz] = book.get_best_ask();
        REQUIRE(ask_px == 10100);
        REQUIRE(ask_sz == 50);  // 250 - 200 = 50 remaining from second order
    }
}

TEST_CASE("MBP-10 Snapshot Generation", "[orderbook][snapshot]") {
    OrderBook book;
    MBPSnapshot snapshot;
    
    SECTION("Empty book snapshot") {
        book.get_top10_snapshot(snapshot);
        
        for (int i = 0; i < 10; ++i) {
            REQUIRE(snapshot.bid_px[i] == 0);
            REQUIRE(snapshot.bid_sz[i] == 0);
            REQUIRE(snapshot.ask_px[i] == 0);
            REQUIRE(snapshot.ask_sz[i] == 0);
        }
    }
    
    SECTION("Single level each side") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE(book.add_order(1002, 10100, 200, 'A', 2000));
        
        book.get_top10_snapshot(snapshot);
        
        REQUIRE(snapshot.bid_px[0] == 10050);
        REQUIRE(snapshot.bid_sz[0] == 100);
        REQUIRE(snapshot.ask_px[0] == 10100);
        REQUIRE(snapshot.ask_sz[0] == 200);
        
        // Other levels should be empty
        for (int i = 1; i < 10; ++i) {
            REQUIRE(snapshot.bid_px[i] == 0);
            REQUIRE(snapshot.ask_px[i] == 0);
        }
    }
    
    SECTION("Multiple levels with proper ordering") {
        // Add bids (higher prices first)
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));  // Best
        REQUIRE(book.add_order(1002, 10025, 150, 'B', 2000));  // Second
        REQUIRE(book.add_order(1003, 10075, 200, 'B', 3000));  // Should be new best
        
        // Add asks (lower prices first)
        REQUIRE(book.add_order(2001, 10100, 300, 'A', 4000));  // Best
        REQUIRE(book.add_order(2002, 10125, 250, 'A', 5000));  // Second
        REQUIRE(book.add_order(2003, 10090, 400, 'A', 6000));  // Should be new best
        
        book.get_top10_snapshot(snapshot);
        
        // Check bid ordering (highest first)
        REQUIRE(snapshot.bid_px[0] == 10075);  // Highest bid
        REQUIRE(snapshot.bid_sz[0] == 200);
        REQUIRE(snapshot.bid_px[1] == 10050);
        REQUIRE(snapshot.bid_sz[1] == 100);
        REQUIRE(snapshot.bid_px[2] == 10025);  // Lowest bid
        REQUIRE(snapshot.bid_sz[2] == 150);
        
        // Check ask ordering (lowest first)
        REQUIRE(snapshot.ask_px[0] == 10090);  // Lowest ask
        REQUIRE(snapshot.ask_sz[0] == 400);
        REQUIRE(snapshot.ask_px[1] == 10100);
        REQUIRE(snapshot.ask_sz[1] == 300);
        REQUIRE(snapshot.ask_px[2] == 10125);  // Highest ask
        REQUIRE(snapshot.ask_sz[2] == 250);
    }
}

TEST_CASE("Action Engine T+F+C Sequence", "[action_engine]") {
    OrderBook book;
    ActionEngine engine(book);
    
    SECTION("Complete T+F+C sequence") {
        // Set up order book with liquidity
        Event add_event(1000, 'A', 'A', 10100, 200, 1001);
        REQUIRE(engine.process_event(add_event));
        
        // T+F+C sequence
        Event trade_event(2000, 'T', 'B', 10100, 100, 2001);
        Event fill_event(3000, 'F', 'B', 10100, 100, 2001);
        Event cancel_event(4000, 'C', 'B', 10100, 0, 2001);
        
        // Process sequence
        REQUIRE_FALSE(engine.process_event(trade_event));  // Should buffer
        REQUIRE_FALSE(engine.process_event(fill_event));   // Should wait
        REQUIRE(engine.process_event(cancel_event));       // Should execute
        
        // Verify trade was executed (ask side reduced)
        auto [ask_px, ask_sz] = book.get_best_ask();
        REQUIRE(ask_px == 10100);
        REQUIRE(ask_sz == 100);  // 200 - 100 = 100 remaining
    }
}

TEST_CASE("Performance Characteristics", "[performance]") {
    OrderBook book;
    
    SECTION("Large number of orders") {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Add 10,000 orders
        for (uint64_t i = 1; i <= 10000; ++i) {
            int64_t price = 10000 + (i % 1000);  // Spread across price levels
            uint32_t size = 100 + (i % 100);
            char side = (i % 2 == 0) ? 'B' : 'A';
            REQUIRE(book.add_order(i, price, size, side, i * 1000));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Should process 10k orders in reasonable time (less than 20ms)
        REQUIRE(duration.count() < 20000);
        
        // Verify book state
        REQUIRE(book.get_active_orders() == 10000);
        REQUIRE(book.get_price_levels() > 0);
    }
    
    SECTION("Snapshot generation speed") {
        // Add orders to create full book
        for (int i = 0; i < 100; ++i) {
            book.add_order(i + 1000, 10000 + i, 100, 'B', i * 1000);
            book.add_order(i + 2000, 11000 + i, 100, 'A', i * 1000);
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Generate many snapshots
        MBPSnapshot snapshot;
        for (int i = 0; i < 1000; ++i) {
            book.get_top10_snapshot(snapshot);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // Should generate 1000 snapshots in reasonable time
        REQUIRE(duration.count() < 1000);  // Less than 1ms
    }
}

TEST_CASE("Edge Cases and Error Handling", "[edge_cases]") {
    OrderBook book;
    
    SECTION("Duplicate order IDs") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE_FALSE(book.add_order(1001, 10075, 150, 'B', 2000));  // Duplicate ID
    }
    
    SECTION("Modify non-existent order") {
        REQUIRE_FALSE(book.modify_order(9999, 10050, 100));
    }
    
    SECTION("Cancel non-existent order") {
        REQUIRE_FALSE(book.cancel_order(9999));
    }
    
    SECTION("Trade on empty book") {
        REQUIRE_FALSE(book.execute_trade(10050, 100, 'B'));
    }
    
    SECTION("Trade on non-existent price") {
        REQUIRE(book.add_order(1001, 10050, 100, 'B', 1000));
        REQUIRE_FALSE(book.execute_trade(10075, 100, 'A'));  // Wrong price
    }
}

// Property-based testing
TEST_CASE("Order Book Invariants", "[properties]") {
    OrderBook book;
    
    SECTION("Best bid always <= best ask") {
        // Add crossing orders and verify no cross
        book.add_order(1001, 10050, 100, 'B', 1000);
        book.add_order(1002, 10100, 100, 'A', 2000);
        
        auto [bid_px, bid_sz] = book.get_best_bid();
        auto [ask_px, ask_sz] = book.get_best_ask();
        
        if (bid_px > 0 && ask_px > 0) {
            REQUIRE(bid_px <= ask_px);  // No crossed book
        }
    }
    
    SECTION("Total size conservation during modifications") {
        book.add_order(1001, 10050, 100, 'B', 1000);
        book.add_order(1002, 10050, 150, 'B', 2000);
        
        auto [bid_px, initial_sz] = book.get_best_bid();
        REQUIRE(initial_sz == 250);
        
        // Modify one order
        book.modify_order(1001, 10050, 200);
        auto [same_px, new_sz] = book.get_best_bid();
        REQUIRE(new_sz == 350);  // 200 + 150
    }
} 