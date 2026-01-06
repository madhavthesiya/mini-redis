#pragma once
#include <string>
#include "Result.h"

class StorageInterface {
    public:
    virtual Result set(const std::string& key, const std::string& value)=0;
    virtual Result get(const std::string& key)=0;
    virtual Result del(const std::string& key)=0;
    virtual Result exists(const std::string& key)=0;
    virtual Result saveToFile(const std::string& filename)=0;
    virtual Result loadFromFile(const std::string& filename)=0;

    virtual ~StorageInterface() = default;
};