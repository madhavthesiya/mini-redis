#pragma once
#include <string>

enum class ErrorCode{
    NONE,
    KEY_NOT_FOUND,
    INVALID_KEY,
    FILE_ERROR,
};

struct Result{
    bool success;
    std::string value;
    ErrorCode error;

    static Result ok(const std::string& val = "") { return {true, val, ErrorCode::NONE}; }
    static Result fail(ErrorCode err) { return {false, "", err}; }
};