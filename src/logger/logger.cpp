#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cerrno>
#include <system_error>

Logger::Logger(const std::filesystem::path& path)
    : _fd(open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644)) {
    if (_fd == -1) {
        throw std::system_error(errno, std::generic_category(), "open log");
    }
}

Logger::~Logger() { close(_fd); }

void Logger::Write(const std::string& message) const {
    std::string clean = message.substr(0, 1500);

    for (char& ch : clean) {
        if (static_cast<unsigned char>(ch) < 32 || ch == 127) {
            ch = '?';
        }
    }

    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    const std::string line = std::string(timestamp) + " pid=" +
        std::to_string(getpid()) + " " + clean + "\n";
    ssize_t result;
    do {
        result = write(_fd, line.data(), line.size());
    } while (result == -1 && errno == EINTR);

}
