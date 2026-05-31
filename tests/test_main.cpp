#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>
#include "../InMemoryStorage.h"
#include "../RedisLite.h"
#include "../RespParser.h"

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
    InMemoryStorage s(2);
    s.set("A", "1");
    s.set("B", "2");
    s.set("C", "3");

    Result r = s.get("A");
    ASSERT_FALSE(r.success);
    r = s.get("B");
    ASSERT_TRUE(r.success);
    r = s.get("C");
    ASSERT_TRUE(r.success);
}

TEST(test_lru_refresh_with_get) {
    InMemoryStorage s(2);
    s.set("A", "1");
    s.set("B", "2");
    s.get("A");
    s.set("C", "3");

    Result r = s.get("A");
    ASSERT_TRUE(r.success);
    r = s.get("B");
    ASSERT_FALSE(r.success);
}

TEST(test_exists_does_not_refresh_lru) {
    InMemoryStorage s(2);
    s.set("A", "1");
    s.set("B", "2");
    s.exists("A");
    s.set("C", "3");

    Result r = s.get("A");
    ASSERT_FALSE(r.success);
}

TEST(test_lru_capacity_one) {
    InMemoryStorage s(1);
    s.set("A", "1");
    s.set("B", "2");

    Result r = s.get("A");
    ASSERT_FALSE(r.success);
    r = s.get("B");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "2");
}

// ==================== TTL Expiration ====================

TEST(test_ttl_expiration) {
    InMemoryStorage s(10);
    s.set("temp", "data", 1);
    
    Result r = s.get("temp");
    ASSERT_TRUE(r.success);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    r = s.get("temp");
    ASSERT_FALSE(r.success);
}

TEST(test_ttl_check_remaining) {
    InMemoryStorage s(10);
    s.set("key", "val", 10);

    Result r = s.ttl("key");
    ASSERT_TRUE(r.success);
    int remaining = std::stoi(r.value);
    ASSERT_TRUE(remaining >= 8 && remaining <= 10);
}

TEST(test_ttl_no_expiry) {
    InMemoryStorage s(10);
    s.set("key", "val");

    Result r = s.ttl("key");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "-1");
}

TEST(test_expire_existing_key) {
    InMemoryStorage s(10);
    s.set("key", "val");

    s.expire("key", 1);
    Result r = s.get("key");
    ASSERT_TRUE(r.success);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    r = s.get("key");
    ASSERT_FALSE(r.success);
}

TEST(test_exists_removes_expired_key) {
    InMemoryStorage s(10);
    s.set("key", "val", 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    Result r = s.exists("key");
    ASSERT_FALSE(r.success);
}

// ==================== TTL > LRU Priority ====================

TEST(test_ttl_evicted_before_lru) {
    InMemoryStorage s(2);
    s.set("A", "1", 1);
    s.set("B", "2");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    s.set("C", "3");

    Result r = s.get("A");
    ASSERT_FALSE(r.success);
    r = s.get("B");
    ASSERT_TRUE(r.success);
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
        InMemoryStorage s(2);
        s.loadFromFile(filename);

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
        s.set("dead", "no", 1);

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
        ASSERT_FALSE(r.success);
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

// ==================== List Operations ====================

TEST(test_lpush_and_lrange) {
    InMemoryStorage s(10);
    Result r = s.lpush("mylist", {"c", "b", "a"});
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "3");  // length after push

    r = s.lrange("mylist", 0, -1);  // get all elements
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.values.size(), (size_t)3);
    // LPUSH pushes each in order, so last pushed is first: a, b, c
    ASSERT_EQ(r.values[0], "a");
    ASSERT_EQ(r.values[1], "b");
    ASSERT_EQ(r.values[2], "c");
}

TEST(test_rpush_and_lrange) {
    InMemoryStorage s(10);
    s.rpush("mylist", {"a", "b", "c"});

    Result r = s.lrange("mylist", 0, -1);
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.values.size(), (size_t)3);
    ASSERT_EQ(r.values[0], "a");
    ASSERT_EQ(r.values[1], "b");
    ASSERT_EQ(r.values[2], "c");
}

TEST(test_lpop_and_rpop) {
    InMemoryStorage s(10);
    s.rpush("mylist", {"a", "b", "c"});

    Result r = s.lpop("mylist");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "a");

    r = s.rpop("mylist");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "c");

    r = s.llen("mylist");
    ASSERT_EQ(r.value, "1");  // only "b" remains
}

TEST(test_lpop_empty_list_deletes_key) {
    InMemoryStorage s(10);
    s.rpush("mylist", {"only"});
    s.lpop("mylist");

    // Key should be auto-deleted when list becomes empty (Redis behavior)
    Result r = s.exists("mylist");
    ASSERT_FALSE(r.success);
}

TEST(test_lrange_negative_indices) {
    InMemoryStorage s(10);
    s.rpush("mylist", {"a", "b", "c", "d", "e"});

    // -2 means second-to-last, -1 means last
    Result r = s.lrange("mylist", -3, -1);
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.values.size(), (size_t)3);
    ASSERT_EQ(r.values[0], "c");
    ASSERT_EQ(r.values[1], "d");
    ASSERT_EQ(r.values[2], "e");
}

TEST(test_llen) {
    InMemoryStorage s(10);
    Result r = s.llen("empty");
    ASSERT_EQ(r.value, "0");

    s.rpush("mylist", {"a", "b"});
    r = s.llen("mylist");
    ASSERT_EQ(r.value, "2");
}

// ==================== Set Operations ====================

TEST(test_sadd_and_smembers) {
    InMemoryStorage s(10);
    Result r = s.sadd("myset", {"a", "b", "c"});
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "3");  // all 3 added

    r = s.smembers("myset");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.values.size(), (size_t)3);
}

TEST(test_sadd_duplicates) {
    InMemoryStorage s(10);
    s.sadd("myset", {"a", "b"});
    Result r = s.sadd("myset", {"b", "c"});  // "b" already exists
    ASSERT_EQ(r.value, "1");  // only "c" was newly added
}

TEST(test_srem) {
    InMemoryStorage s(10);
    s.sadd("myset", {"a", "b", "c"});
    Result r = s.srem("myset", {"b", "x"});  // "x" doesn't exist
    ASSERT_EQ(r.value, "1");  // only "b" was removed

    r = s.scard("myset");
    ASSERT_EQ(r.value, "2");
}

TEST(test_sismember) {
    InMemoryStorage s(10);
    s.sadd("myset", {"a", "b"});

    Result r = s.sismember("myset", "a");
    ASSERT_EQ(r.value, "1");  // exists

    r = s.sismember("myset", "z");
    ASSERT_EQ(r.value, "0");  // doesn't exist
}

TEST(test_srem_empty_set_deletes_key) {
    InMemoryStorage s(10);
    s.sadd("myset", {"only"});
    s.srem("myset", {"only"});

    Result r = s.exists("myset");
    ASSERT_FALSE(r.success);  // key auto-deleted
}

// ==================== Hash Operations ====================

TEST(test_hset_and_hget) {
    InMemoryStorage s(10);
    Result r = s.hset("user", {{"name", "madhav"}, {"age", "21"}});
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "2");  // 2 new fields added

    r = s.hget("user", "name");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "madhav");

    r = s.hget("user", "age");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "21");
}

TEST(test_hset_overwrite_field) {
    InMemoryStorage s(10);
    s.hset("user", {{"name", "old"}});
    Result r = s.hset("user", {{"name", "new"}});
    ASSERT_EQ(r.value, "0");  // 0 new fields (overwritten existing)

    r = s.hget("user", "name");
    ASSERT_EQ(r.value, "new");
}

TEST(test_hdel) {
    InMemoryStorage s(10);
    s.hset("user", {{"name", "madhav"}, {"age", "21"}, {"lang", "cpp"}});
    Result r = s.hdel("user", {"age", "ghost"});
    ASSERT_EQ(r.value, "1");  // only "age" was deleted

    r = s.hlen("user");
    ASSERT_EQ(r.value, "2");
}

TEST(test_hgetall) {
    InMemoryStorage s(10);
    s.hset("user", {{"name", "madhav"}, {"lang", "cpp"}});

    Result r = s.hgetall("user");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.values.size(), (size_t)4);  // 2 field-value pairs = 4 elements
}

TEST(test_hget_missing_field) {
    InMemoryStorage s(10);
    s.hset("user", {{"name", "madhav"}});

    Result r = s.hget("user", "phone");
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::KEY_NOT_FOUND);
}

// ==================== Type Safety ====================

TEST(test_type_command) {
    InMemoryStorage s(10);
    s.set("str", "hello");
    s.lpush("lst", {"a"});
    s.sadd("st", {"x"});
    s.hset("hs", {{"k", "v"}});

    ASSERT_EQ(s.type("str").value, "string");
    ASSERT_EQ(s.type("lst").value, "list");
    ASSERT_EQ(s.type("st").value, "set");
    ASSERT_EQ(s.type("hs").value, "hash");
    ASSERT_EQ(s.type("nope").value, "none");
}

TEST(test_wrong_type_get_on_list) {
    InMemoryStorage s(10);
    s.lpush("mylist", {"a"});

    Result r = s.get("mylist");  // GET is a string op — can't use on list
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::WRONG_TYPE);
}

TEST(test_wrong_type_lpush_on_string) {
    InMemoryStorage s(10);
    s.set("name", "madhav");

    Result r = s.lpush("name", {"oops"});  // LPUSH on a string key
    ASSERT_FALSE(r.success);
    ASSERT_EQ(r.error, ErrorCode::WRONG_TYPE);
}

TEST(test_set_overwrites_any_type) {
    InMemoryStorage s(10);
    s.lpush("key", {"a", "b"});

    // SET should overwrite regardless of existing type (Redis behavior)
    s.set("key", "string_now");
    Result r = s.get("key");
    ASSERT_TRUE(r.success);
    ASSERT_EQ(r.value, "string_now");
    ASSERT_EQ(s.type("key").value, "string");
}

// ==================== LRU with Mixed Types ====================

TEST(test_lru_evicts_across_types) {
    InMemoryStorage s(2);
    s.set("str", "hello");
    s.sadd("myset", {"a"});
    // Cache is full (2 keys). Adding a 3rd should evict "str" (LRU)
    s.lpush("mylist", {"x"});

    Result r = s.get("str");
    ASSERT_FALSE(r.success);  // evicted

    r = s.scard("myset");
    ASSERT_EQ(r.value, "1");  // still alive

    r = s.llen("mylist");
    ASSERT_EQ(r.value, "1");  // still alive
}

// ==================== AOF Persistence ====================

// Helper: count RESP commands in raw AOF data
size_t countRespCommands(const std::string& raw) {
    std::string buf = raw;
    size_t count = 0;
    while(!buf.empty()) {
        auto [args, consumed] = RespParser::parse(buf);
        if(consumed == 0) break;
        buf.erase(0, consumed);
        if(!args.empty()) count++;
    }
    return count;
}

TEST(test_aof_basic_replay) {
    const std::string aof_file = "test_aof_basic.aof";
    std::remove(aof_file.c_str());
    // Phase 1: Write with AOF logging
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.set("name", "madhav");
        s.set("lang", "cpp");
        s.del("lang");
    }
    // Phase 2: Create fresh storage, replay AOF — state should be restored
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.replayAOF();

        Result r = s.get("name");
        ASSERT_TRUE(r.success);
        ASSERT_EQ(r.value, "madhav");

        r = s.get("lang");
        ASSERT_FALSE(r.success);  // was deleted
    }
    std::remove(aof_file.c_str());
}

TEST(test_aof_all_types) {
    const std::string aof_file = "test_aof_types.aof";
    std::remove(aof_file.c_str());
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.set("str", "hello");
        s.rpush("lst", {"a", "b", "c"});
        s.sadd("st", {"x", "y"});
        s.hset("hs", {{"name", "madhav"}});
        s.lpop("lst");  // removes "a"
    }
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.replayAOF();

        ASSERT_EQ(s.get("str").value, "hello");
        ASSERT_EQ(s.llen("lst").value, "2");       // "b" and "c" remain
        ASSERT_EQ(s.scard("st").value, "2");
        ASSERT_EQ(s.hget("hs", "name").value, "madhav");
    }
    std::remove(aof_file.c_str());
}

TEST(test_aof_rewrite_compaction) {
    const std::string aof_file = "test_aof_rewrite.aof";
    std::remove(aof_file.c_str());
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        // Write 10 redundant commands
        s.set("key", "v1");
        s.set("key", "v2");
        s.set("key", "v3");
        s.set("key", "v4");
        s.set("key", "final");

        // Before rewrite: 5 commands in AOF
        ASSERT_EQ(countRespCommands(aof.readRaw()), (size_t)5);

        // Rewrite compacts to minimal state
        auto commands = s.dumpState();
        aof.rewrite(commands);

        ASSERT_EQ(countRespCommands(aof.readRaw()), (size_t)1);  // only 1 SET needed
    }
    // Verify compacted AOF still restores correctly
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.replayAOF();
        ASSERT_EQ(s.get("key").value, "final");
    }
    std::remove(aof_file.c_str());
}

TEST(test_aof_no_log_on_failed_ops) {
    const std::string aof_file = "test_aof_nolog.aof";
    std::remove(aof_file.c_str());
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.set("key", "val");
        s.del("nonexistent");       // fails — should NOT be logged
        s.get("nonexistent");       // read — should NOT be logged
        s.lpush("key", {"oops"});   // WRONG_TYPE — should NOT be logged

        ASSERT_EQ(countRespCommands(aof.readRaw()), (size_t)1);  // only the successful SET
    }
    std::remove(aof_file.c_str());
}

TEST(test_aof_rewrite_preserves_ttl) {
    // This test would have CAUGHT the TTL bug if it existed before the fix.
    // dumpState() must emit EXPIRE commands for keys with remaining TTL.
    const std::string aof_file = "test_aof_ttl.aof";
    std::remove(aof_file.c_str());
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.set("persistent", "no_ttl");
        s.set("expiring", "has_ttl", 300);  // 5 minute TTL

        // Rewrite compacts the AOF
        auto commands = s.dumpState();
        aof.rewrite(commands);

        auto raw = aof.readRaw();
        bool found_expire = false;
        std::string buf = raw;
        while(!buf.empty()) {
            auto [args, consumed] = RespParser::parse(buf);
            if(consumed == 0) break;
            buf.erase(0, consumed);
            if(!args.empty() && args[0] == "EXPIRE" && args.size() >= 2 && args[1] == "expiring") {
                found_expire = true;
            }
        }
        ASSERT_TRUE(found_expire);  // TTL must survive rewrite
    }
    // Verify replaying compacted AOF restores TTL
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.replayAOF();

        Result r = s.ttl("persistent");
        ASSERT_EQ(r.value, "-1");  // no TTL

        r = s.ttl("expiring");
        ASSERT_TRUE(r.success);
        int remaining = std::stoi(r.value);
        ASSERT_TRUE(remaining > 0);  // TTL survived rewrite + replay
    }
    std::remove(aof_file.c_str());
}

TEST(test_aof_replay_without_file) {
    InMemoryStorage s(10);  // no AOF
    Result r = s.replayAOF();
    ASSERT_FALSE(r.success);  // no AOF configured
}

TEST(test_aof_values_with_spaces) {
    // THIS TEST would FAIL with the old plain-text AOF format.
    // RESP format is binary-safe — length-prefixed, so spaces are preserved.
    const std::string aof_file = "test_aof_spaces.aof";
    std::remove(aof_file.c_str());
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.set("greeting", "hello world");   // value with space
        s.set("quote", "to be or not to be");
        s.rpush("words", {"foo bar", "baz qux"});
        s.hset("user", {{"bio", "loves c++"}});
    }
    {
        AOFLogger aof(aof_file);
        InMemoryStorage s(10, &aof);
        s.replayAOF();

        ASSERT_EQ(s.get("greeting").value, "hello world");
        ASSERT_EQ(s.get("quote").value, "to be or not to be");

        auto lr = s.lrange("words", 0, -1);
        ASSERT_EQ(lr.values.size(), (size_t)2);
        ASSERT_EQ(lr.values[0], "foo bar");
        ASSERT_EQ(lr.values[1], "baz qux");

        ASSERT_EQ(s.hget("user", "bio").value, "loves c++");
    }
    std::remove(aof_file.c_str());
}

// ==================== RESP Parser ====================

TEST(test_resp_parse_array) {
    // *3\r\n$3\r\nSET\r\n$4\r\nname\r\n$6\r\nmadhav\r\n
    std::string input = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$6\r\nmadhav\r\n";
    auto [args, consumed] = RespParser::parse(input);
    ASSERT_EQ(args.size(), (size_t)3);
    ASSERT_EQ(args[0], "SET");
    ASSERT_EQ(args[1], "name");
    ASSERT_EQ(args[2], "madhav");
    ASSERT_EQ(consumed, input.size());
}

TEST(test_resp_parse_inline) {
    std::string input = "PING\r\n";
    auto [args, consumed] = RespParser::parse(input);
    ASSERT_EQ(args.size(), (size_t)1);
    ASSERT_EQ(args[0], "PING");
    ASSERT_EQ(consumed, input.size());
}

TEST(test_resp_parse_incomplete) {
    // Half a RESP command — parser should return 0 consumed (need more data)
    std::string input = "*3\r\n$3\r\nSET\r\n$4\r\n";
    auto [args, consumed] = RespParser::parse(input);
    ASSERT_EQ(consumed, (size_t)0);  // incomplete — don't consume anything
    ASSERT_TRUE(args.empty());
}

TEST(test_resp_parse_multiple_commands) {
    // Two commands in one buffer (common with TCP pipelining)
    std::string input = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$4\r\nname\r\n";
    auto [args1, consumed1] = RespParser::parse(input);
    ASSERT_EQ(args1[0], "PING");
    ASSERT_TRUE(consumed1 > 0);

    std::string remaining = input.substr(consumed1);
    auto [args2, consumed2] = RespParser::parse(remaining);
    ASSERT_EQ(args2[0], "GET");
    ASSERT_EQ(args2[1], "name");
}

TEST(test_resp_serialize) {
    ASSERT_EQ(RespParser::simpleString("OK"), "+OK\r\n");
    ASSERT_EQ(RespParser::error("ERR bad"), "-ERR bad\r\n");
    ASSERT_EQ(RespParser::integer(42), ":42\r\n");
    ASSERT_EQ(RespParser::nullBulk(), "$-1\r\n");
    ASSERT_EQ(RespParser::bulkString("hi"), "$2\r\nhi\r\n");
}

// ==================== Main ======================================

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

    std::cout << "\n[List Operations]\n";
    RUN_TEST(test_lpush_and_lrange);
    RUN_TEST(test_rpush_and_lrange);
    RUN_TEST(test_lpop_and_rpop);
    RUN_TEST(test_lpop_empty_list_deletes_key);
    RUN_TEST(test_lrange_negative_indices);
    RUN_TEST(test_llen);

    std::cout << "\n[Set Operations]\n";
    RUN_TEST(test_sadd_and_smembers);
    RUN_TEST(test_sadd_duplicates);
    RUN_TEST(test_srem);
    RUN_TEST(test_sismember);
    RUN_TEST(test_srem_empty_set_deletes_key);

    std::cout << "\n[Hash Operations]\n";
    RUN_TEST(test_hset_and_hget);
    RUN_TEST(test_hset_overwrite_field);
    RUN_TEST(test_hdel);
    RUN_TEST(test_hgetall);
    RUN_TEST(test_hget_missing_field);

    std::cout << "\n[Type Safety]\n";
    RUN_TEST(test_type_command);
    RUN_TEST(test_wrong_type_get_on_list);
    RUN_TEST(test_wrong_type_lpush_on_string);
    RUN_TEST(test_set_overwrites_any_type);

    std::cout << "\n[LRU with Mixed Types]\n";
    RUN_TEST(test_lru_evicts_across_types);

    std::cout << "\n[AOF Persistence]\n";
    RUN_TEST(test_aof_basic_replay);
    RUN_TEST(test_aof_all_types);
    RUN_TEST(test_aof_rewrite_compaction);
    RUN_TEST(test_aof_no_log_on_failed_ops);
    RUN_TEST(test_aof_rewrite_preserves_ttl);
    RUN_TEST(test_aof_replay_without_file);
    RUN_TEST(test_aof_values_with_spaces);

    std::cout << "\n[RESP Parser]\n";
    RUN_TEST(test_resp_parse_array);
    RUN_TEST(test_resp_parse_inline);
    RUN_TEST(test_resp_parse_incomplete);
    RUN_TEST(test_resp_parse_multiple_commands);
    RUN_TEST(test_resp_serialize);

    std::cout << "\n==========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "==========================================\n\n";

    return failed > 0 ? 1 : 0;
}
