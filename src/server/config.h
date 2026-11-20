#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

struct ServerConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::filesystem::path document_root = "./public";
    std::size_t workers = 4;
    std::size_t max_connections_per_worker = 256;
    std::chrono::seconds client_timeout{30};
    std::filesystem::path log_path = "./server.log";
};

