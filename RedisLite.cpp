#include "RedisLite.h"

RedisLite::RedisLite(StorageInterface& storage)
    : storage_(storage) {}

Result RedisLite::exists(const std::string& key) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.exists(key);
}

Result RedisLite::get(const std::string& key) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.get(key);
}

Result RedisLite::set(const std::string& key, const std::string& value) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.set(key, value);
}

Result RedisLite::setex(const std::string& key, int ttlSeconds, const std::string& value) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    if(ttlSeconds <= 0) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.set(key, value, ttlSeconds);
}

Result RedisLite::del(const std::string& key) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.del(key);
}

Result RedisLite::expire(const std::string& key, int ttlSeconds) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    if(ttlSeconds <= 0) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.expire(key, ttlSeconds);
}

Result RedisLite::ttl(const std::string& key) {
    if(key.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.ttl(key);
}

Result RedisLite::save(const std::string& filename) {
    if(filename.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.saveToFile(filename);
}

Result RedisLite::load(const std::string& filename) {
    if(filename.empty()) return Result::fail(ErrorCode::INVALID_KEY);
    return storage_.loadFromFile(filename);
}