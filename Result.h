#pragma once
#include <string>

enum class ErrorCode{
    NONE,
    KEY_NOT_FOUND,
    INVALID_KEY,
};

struct Result{
    bool success;
    std:: string value;
    ErrorCode error;
};