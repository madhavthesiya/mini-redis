#pragma once
#include "StorageInterface.h"

class RedisLite{
    public:
        explicit RedisLite(StorageInterface& storage);
        Result set(const std::string& key, const std::string& value);
        Result get(const std::string& key);
        Result del(const std::string& key);
        Result exists(const std::string& key);
        Result save(const std::string& filename);
        Result load(const std::string& filename);
    private:
        StorageInterface& storage_;
};