#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>
#include "../InMemoryStorage.h"
#include "../RedisLite.h"

int passed = 0;
int failed = 0;

#define TEST(name) void name()
#define RUN_TEST(name) \
    std::cout << "  Running " << #name << "..."; \
    try { name(); passed++; std::cout << " PASSED" << std::endl; } \
    catch(const std::exception& e) { failed++; std::cout << " FAILED: " << e.what() << std::endl; }

#define ASSERT_TRUE(expr) if(!(expr)) throw std::runtime_error("Assertion failed: " #expr)
#define ASSERT_FALSE(expr) if(expr) throw std::runtime_error("Assertion failed: NOT " #expr)
#define ASSERT_EQ(a, b) if((a) != (b)) throw std::runtime_error("Assertion failed: " #a " != " #b)

// ==================== Basic Operations ====================

TEST(test_set_and_get) {
    InMemoryStorage s(10);
    s.set("name", "madhav");
    Result r = s.get("name");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "madhav");
}

TEST(test_get_nonexistent_key) {
    InMemoryStorage s(10);
    Result r = s.get("missing");
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::KEY_NOT_FOUND);
}

TEST(test_overwrite_existing_key) {
    InMemoryStorage s(10);
    s.set("key", "v1");
    s.set("key", "v2");
    Result r = s.get("key");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "v2");
}

TEST(test_delete_key) {
    InMemoryStorage s(10);
    s.set("key", "val");
    Result r = s.del("key");
    ASSERT_TRUE(r.success);
    r = s.get("key");
    ASSERT_FALSE(r.success);
}

TEST(test_delete_nonexistent_key) {
    InMemoryStorage s(10);
    Result r = s.del("ghost");
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::KEY_NOT_FOUND);
}

TEST(test_exists_found) {
    InMemoryStorage s(10);
    s.set("key", "val");
    Result r = s.exists("key");
    ASSERT_TRUE(r.success);
}

TEST(test_exists_not_found) {
    InMemoryStorage s(10);
    Result r = s.exists("nope");
    ASSERT_FALSE(r.success);
}

// ==================== LRU Eviction ====================

TEST(test_lru_eviction_basic) {
    InMemoryStorage s(2);   // capacity = 2
    s.set("A", "1");
    s.set("B", "2");
    s.set("C", "3");        // should evict A (least recently used)

    Result r = s.get("A");
    ASSERT_FALSE(r.success);    // A should be gone

    r = s.get("B");
    ASSERT_TRUE(r.success);     // B should survive

    r = s.get("C");
    ASSERT_TRUE(r.success);     // C should exist
}

TEST(test_lru_refresh_with_get) {
    InMemoryStorage s(2);
    s.set("A", "1");
    s.set("B", "2");
    s.get("A");             // refresh A — now B is the least recently used
    s.set("C", "3");        // should evict B, not A

    Result r = s.get("A");
    ASSERT_TRUE(r.success);     // A survived (was refreshed)

    r = s.get("B");
    ASSERT_FALSE(r.success);    // B was evicted
}

TEST(test_exists_does_not_refresh_lru) {
    InMemoryStorage s(2);
    s.set("A", "1");
    s.set("B", "2");
    s.exists("A");          // EXISTS should NOT refresh LRU
    s.set("C", "3");        // should evict A (still the oldest)

    Result r = s.get("A");
    ASSERT_FALSE(r.success);    // A evicted — exists didn't save it
}

TEST(test_lru_capacity_one) {
    InMemoryStorage s(1);
    s.set("A", "1");
    s.set("B", "2");        // evicts A

    Result r = s.get("A");
    ASSERT_FALSE(r.success);

    r = s.get("B");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "2");
}

// ==================== TTL Expiration ====================

TEST(test_ttl_expiration) {
    InMemoryStorage s(10);
    s.set("temp", "data", 1);   // expires in 1 second
    
    Result r = s.get("temp");
    ASSERT_TRUE(r.success);     // should exist immediately

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    r = s.get("temp");
    ASSERT_FALSE(r.success);    // should be expired now
}

TEST(test_ttl_check_remaining) {
    InMemoryStorage s(10);
    s.set("key", "val", 10);   // 10 second TTL

    Result r = s.ttl("key");
    ASSERT_TRUE(r.success);
    int remaining = std::stoi(r.value);
    ASSERT_TRUE(remaining >= 8 && remaining <= 10);     // should be ~10
}

TEST(test_ttl_no_expiry) {
    InMemoryStorage s(10);
    s.set("key", "val");       // no TTL

    Result r = s.ttl("key");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "-1");   // -1 means no expiry
}

TEST(test_expire_existing_key) {
    InMemoryStorage s(10);
    s.set("key", "val");       // no TTL initially

    s.expire("key", 1);       // set 1 second TTL
    Result r = s.get("key");
    ASSERT_TRUE(r.success);    // still alive

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    r = s.get("key");
    ASSERT_FALSE(r.success);   // expired
}

TEST(test_exists_removes_expired_key) {
    InMemoryStorage s(10);
    s.set("key", "val", 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    Result r = s.exists("key");
    ASSERT_FALSE(r.success);   // expired key removed via lazy expiration
}

// ==================== TTL > LRU Priority ====================

TEST(test_ttl_evicted_before_lru) {
    InMemoryStorage s(2);
    s.set("A", "1", 1);       // A has 1 second TTL
    s.set("B", "2");           // B has no TTL

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    s.set("C", "3");           // should evict expired A, not LRU B

    Result r = s.get("A");
    ASSERT_FALSE(r.success);   // A expired and evicted

    r = s.get("B");
    ASSERT_TRUE(r.success);    // B survives — TTL eviction saved it
    ASSERT_EQ(r.value, "2");

    r = s.get("C");
    ASSERT_TRUE(r.success);
}

// ==================== Persistence ====================

TEST(test_save_and_load) {
    const std::string filename = "test_save_load.dat";
    {
        InMemoryStorage s(10);
        s.set("name", "madhav");
        s.set("lang", "cpp");
        s.saveToFile(filename);
    }
    {
        InMemoryStorage s(10);
        Result r = s.loadFromFile(filename);
        ASSERT_TRUE(r.success);

        r = s.get("name");
        ASSERT_TRUE(r.success);
        ASSERT_EQ(r.value, "madhav");

        r = s.get("lang");
        ASSERT_TRUE(r.success);
        ASSERT_EQ(r.value, "cpp");
    }
    std::remove(filename.c_str());
}

TEST(test_load_enforces_capacity) {
    const std::string filename = "test_capacity.dat";
    {
        InMemoryStorage s(10);
        s.set("A", "1");
        s.set("B", "2");
        s.set("C", "3");
        s.set("D", "4");
        s.set("E", "5");
        s.saveToFile(filename);
    }
    {
        InMemoryStorage s(2);   // capacity = 2, but file has 5 keys
        s.loadFromFile(filename);

        // Only 2 keys should remain after load (most recent ones kept)
        int count = 0;
        if(s.get("A").success) count++;
        if(s.get("B").success) count++;
        if(s.get("C").success) count++;
        if(s.get("D").success) count++;
        if(s.get("E").success) count++;
        ASSERT_EQ(count, 2);
    }
    std::remove(filename.c_str());
}

TEST(test_save_skips_expired_keys) {
    const std::string filename = "test_expired.dat";
    {
        InMemoryStorage s(10);
        s.set("alive", "yes");
        s.set("dead", "no", 1);    // 1 second TTL

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        s.saveToFile(filename);
    }
    {
        InMemoryStorage s(10);
        s.loadFromFile(filename);

        Result r = s.get("alive");
        ASSERT_TRUE(r.success);
        ASSERT_EQ(r.value, "yes");

        r = s.get("dead");
        ASSERT_FALSE(r.success);   // expired key was not persisted
    }
    std::remove(filename.c_str());
}

TEST(test_load_nonexistent_file) {
    InMemoryStorage s(10);
    Result r = s.loadFromFile("does_not_exist.dat");
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::FILE_ERROR);
}

// ==================== Controller Validation ====================

TEST(test_controller_empty_key) {
    InMemoryStorage s(10);
    RedisLite redis(s);

    Result r = redis.get("");
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::INVALID_KEY);

    r = redis.set("", "val");
    ASSERT_FALSE(r.success);

    r = redis.del("");
    ASSERT_FALSE(r.success);

    r = redis.exists("");
    ASSERT_FALSE(r.success);
}

// ==================== Main ====================

int main() {
    std::cout << "\n========== Mini-Redis Test Suite ==========\n\n";

    std::cout << "[Basic Operations]\n";
    RUN_TEST(test_set_and_get);
    RUN_TEST(test_get_nonexistent_key);
    RUN_TEST(test_overwrite_existing_key);
    RUN_TEST(test_delete_key);
    RUN_TEST(test_delete_nonexistent_key);
    RUN_TEST(test_exists_found);
    RUN_TEST(test_exists_not_found);

    std::cout << "\n[LRU Eviction]\n";
    RUN_TEST(test_lru_eviction_basic);
    RUN_TEST(test_lru_refresh_with_get);
    RUN_TEST(test_exists_does_not_refresh_lru);
    RUN_TEST(test_lru_capacity_one);

    std::cout << "\n[TTL Expiration]\n";
    RUN_TEST(test_ttl_expiration);
    RUN_TEST(test_ttl_check_remaining);
    RUN_TEST(test_ttl_no_expiry);
    RUN_TEST(test_expire_existing_key);
    RUN_TEST(test_exists_removes_expired_key);

    std::cout << "\n[TTL > LRU Priority]\n";
    RUN_TEST(test_ttl_evicted_before_lru);

    std::cout << "\n[Persistence]\n";
    RUN_TEST(test_save_and_load);
    RUN_TEST(test_load_enforces_capacity);
    RUN_TEST(test_save_skips_expired_keys);
    RUN_TEST(test_load_nonexistent_file);

    std::cout << "\n[Controller Validation]\n";
    RUN_TEST(test_controller_empty_key);

    std::cout << "\n==========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==========================================\n\n";

    return failed > 0 ? 1 : 0;
}
