#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <iterator>

// Append-Only File logger for crash-safe persistence.
// Every write command is appended to a file in RESP format (binary-safe).
//
// Why RESP format instead of plain text?
//   Plain text: "SET greeting hello world" → on replay, "world" is lost
//   RESP:       "*3\r\n$3\r\nSET\r\n$8\r\ngreeting\r\n$11\r\nhello world\r\n"
//               → length-prefixed, so spaces in values are handled correctly.
//   This is exactly what real Redis does for its AOF file.
//
// Why a separate class?
//   Single Responsibility: AOFLogger handles only file I/O.
//   InMemoryStorage handles only in-memory data operations.
//   They are composed together, not tightly coupled.

class AOFLogger {
public:
    explicit AOFLogger(const std::string& filename)
        : filename_(filename) {
        // Open in append + binary mode — RESP data contains \r\n
        file_.open(filename_, std::ios::app | std::ios::binary);
    }

    ~AOFLogger() {
        if(file_.is_open()) file_.close();
    }

    // Append raw RESP data to the AOF file (thread-safe).
    // The caller is responsible for formatting the data as RESP.
    // No trailing \n is added — RESP already has its own terminators (\r\n).
    void log(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(file_.is_open()) {
            file_.write(data.c_str(), static_cast<std::streamsize>(data.size()));
            file_.flush();      // flush immediately — crash safety
        }
    }

    // Read the entire AOF file as a raw byte string.
    // The caller uses RespParser to extract individual commands.
    std::string readRaw() const {
        std::ifstream in(filename_, std::ios::binary);
        if(!in.is_open()) return "";
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    // Rewrite the AOF file with a compacted set of RESP-formatted commands.
    // Each string in the vector is a complete RESP array (already formatted).
    void rewrite(const std::vector<std::string>& commands) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(file_.is_open()) file_.close();
        std::ofstream out(filename_, std::ios::trunc | std::ios::binary);
        for(const auto& cmd : commands) {
            out.write(cmd.c_str(), static_cast<std::streamsize>(cmd.size()));
        }
        out.close();
        file_.open(filename_, std::ios::app | std::ios::binary);
    }

    std::string getFilename() const { return filename_; }

private:
    std::string filename_;
    std::ofstream file_;
    std::mutex mutex_;      // separate mutex from storage — no contention
};
