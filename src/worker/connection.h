#pragma once
#include "../http/http.h"
#include <chrono>
#include <string>
#include <cstdint>

enum class ConnectionState { ReadingRequest, WritingResponse };

struct Connection {
    int socket = -1;
    int file = -1;
    std::string ip;
    std::string input;
    std::string output;
    std::size_t offset = 0;
    std::uint64_t remaining = 0;
    std::uint64_t sent = 0;
    int status = 0;
    bool closed = false;
    HttpRequest request;
    ConnectionState state = ConnectionState::ReadingRequest;
    std::chrono::steady_clock::time_point activity = std::chrono::steady_clock::now();
};
