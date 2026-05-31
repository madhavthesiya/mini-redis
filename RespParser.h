#pragma once
#include <string>
#include <vector>
#include <sstream>

// RESP (Redis Serialization Protocol) parser and serializer.
//
// RESP is a simple text-based protocol with 5 types:
//   +OK\r\n              → Simple String
//   -ERR message\r\n     → Error
//   :42\r\n              → Integer
//   $6\r\nfoobar\r\n     → Bulk String (length-prefixed binary-safe string)
//   $-1\r\n              → Null Bulk String
//   *3\r\n...            → Array of N elements
//
// Client sends commands as RESP arrays of bulk strings:
//   SET name madhav  →  *3\r\n$3\r\nSET\r\n$4\r\nname\r\n$6\r\nmadhav\r\n
//
// Why header-only? This is a pure utility class with no state.
// No .cpp file needed — keeps the build simple.

class RespParser {
public:
    // ==================== Parsing ====================

    // Parse one RESP command from the buffer.
    // Returns: { parsed arguments, bytes consumed }
    // If buffer contains an incomplete command, returns { {}, 0 }
    //
    // Why return bytes consumed? Because TCP is a stream — one recv() call
    // may contain multiple commands, or half a command. The caller uses
    // "bytes consumed" to know where the next command starts.
    static std::pair<std::vector<std::string>, size_t> parse(const std::string& buffer) {
        if(buffer.empty()) return {{}, 0};

        // RESP commands start with '*' (array). Anything else is inline.
        if(buffer[0] == '*') {
            return parseResp(buffer);
        }
        return parseInline(buffer);
    }

    // ==================== Serialization ====================
    // Each Redis command has a specific response type. These methods
    // let the server construct the correct RESP response.

    static std::string simpleString(const std::string& s) {
        return "+" + s + "\r\n";
    }

    static std::string error(const std::string& s) {
        return "-" + s + "\r\n";
    }

    static std::string integer(int n) {
        return ":" + std::to_string(n) + "\r\n";
    }

    static std::string bulkString(const std::string& s) {
        return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    }

    static std::string nullBulk() {
        return "$-1\r\n";
    }

    static std::string array(const std::vector<std::string>& items) {
        std::string result = "*" + std::to_string(items.size()) + "\r\n";
        for(const auto& item : items) {
            result += bulkString(item);
        }
        return result;
    }

    static std::string emptyArray() {
        return "*0\r\n";
    }

private:
    // Find \r\n in buffer starting at pos. Returns npos if not found.
    static size_t findCRLF(const std::string& buf, size_t pos) {
        return buf.find("\r\n", pos);
    }

    // Parse a RESP array: *N\r\n followed by N bulk strings
    static std::pair<std::vector<std::string>, size_t> parseResp(const std::string& buffer) {
        // Step 1: Read array header  *N\r\n
        size_t crlf = findCRLF(buffer, 0);
        if(crlf == std::string::npos) return {{}, 0};   // incomplete

        int count;
        try {
            count = std::stoi(buffer.substr(1, crlf - 1));
        } catch(...) {
            return {{}, 0};  // malformed
        }
        if(count <= 0) return {{}, crlf + 2};  // empty array, consume the line

        size_t pos = crlf + 2;  // position after *N\r\n
        std::vector<std::string> args;

        // Step 2: Read N bulk strings  $len\r\ndata\r\n
        for(int i = 0; i < count; i++) {
            if(pos >= buffer.size() || buffer[pos] != '$') return {{}, 0};

            crlf = findCRLF(buffer, pos);
            if(crlf == std::string::npos) return {{}, 0};   // incomplete

            int len;
            try {
                len = std::stoi(buffer.substr(pos + 1, crlf - pos - 1));
            } catch(...) {
                return {{}, 0};
            }
            if(len < 0) return {{}, 0};   // null bulk string — malformed in array context

            pos = crlf + 2;  // position after $len\r\n

            // Check we have enough data: len bytes + \r\n
            if(pos + static_cast<size_t>(len) + 2 > buffer.size()) return {{}, 0};  // incomplete

            args.push_back(buffer.substr(pos, len));
            pos = pos + len + 2;  // skip data + \r\n
        }

        return {args, pos};
    }

    // Parse an inline command: "PING\r\n" or "SET key value\r\n"
    // Supports both \r\n and \n line endings (for telnet compatibility)
    static std::pair<std::vector<std::string>, size_t> parseInline(const std::string& buffer) {
        size_t nl = buffer.find('\n');
        if(nl == std::string::npos) return {{}, 0};   // incomplete

        // Extract line (strip \r if present)
        std::string line = buffer.substr(0, nl);
        if(!line.empty() && line.back() == '\r') line.pop_back();

        // Split by whitespace
        std::istringstream iss(line);
        std::vector<std::string> args;
        std::string arg;
        while(iss >> arg) args.push_back(arg);

        return {args, nl + 1};
    }
};
