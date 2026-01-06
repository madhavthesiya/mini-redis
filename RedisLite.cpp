#include "RedisLite.h"
RedisLite::RedisLite(StorageInterface& storage)
    : storage_(storage) {}

    Result RedisLite::exists(const std::string& key) {
        if(key.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.exists(key);
        }
    }

    Result RedisLite::get(const std::string& key){
        if(key.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.get(key);
        }
    }

    Result RedisLite::set(const std::string& key, const std::string& value){
        if(key.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.set(key, value);
        }
    }

    Result RedisLite::del(const std::string& key){
        if(key.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.del(key);
        }
    }

    Result RedisLite::save(const std::string& filename){
        if(filename.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.saveToFile(filename);
        }
    }

    Result RedisLite::load(const std::string& filename){
        if(filename.empty())
        {
            Result res;
            res.success=false;
            res.value="";
            res.error=ErrorCode::INVALID_KEY;
            return res;
        }
        else
        {
            return storage_.loadFromFile(filename);
        }
    }