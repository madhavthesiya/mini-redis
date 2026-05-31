#pragma once
#include <string>
#include <vector>

enum class ErrorCode{
    NONE,
    KEY_NOT_FOUND,
    INVALID_KEY,
    FILE_ERROR,
    WRONG_TYPE,
};

struct Result{
    bool success;
    std::string value;
    ErrorCode error;
    std::vector<std::string> values;    // for multi-value returns (LRANGE, SMEMBERS, HGETALL)

    static Result ok(const std::string& val = "") { return {true, val, ErrorCode::NONE, {}}; }
    static Result ok(int num) { return {true, std::to_string(num), ErrorCode::NONE, {}}; }
    static Result ok(const std::vector<std::string>& vals) { return {true, "", ErrorCode::NONE, vals}; }
    static Result fail(ErrorCode err) { return {false, "", err, {}}; }
};