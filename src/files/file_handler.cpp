#include "file_handler.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <system_error>
#include <sstream>
#include <cctype>

FileHandler::FileHandler(const std::filesystem::path& root)
    : _root(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
    if (_root == -1) {
        throw std::system_error(errno, std::generic_category(), "open document root");
    }
}

FileHandler::~FileHandler() { close(_root); }

int FileHandler::DecodePath(const std::string& target, std::string& path) {
    const auto raw = target.substr(0, target.find('?'));

    for (std::size_t i = 0; i < raw.size(); ++i) {
        unsigned char ch = raw[i];

        if (ch == '%') {
            if (i + 2 >= raw.size()) {
                return 400;
            }

            const std::string digits = "0123456789abcdef";
            const auto high = digits.find(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i + 1]))));
            const auto low = digits.find(static_cast<char>(std::tolower(static_cast<unsigned char>(raw[i + 2]))));

            if (high == std::string::npos || low == std::string::npos) {
                return 400;
            }

            ch = static_cast<unsigned char>(high * 16 + low);
            i += 2;
        }

        if (ch < 32 || ch == 127 || ch == '\\' || ch == '#') {
            return 400;
        }

        path += static_cast<char>(ch);
    }

    if (path.empty() || path.front() != '/') {
        return 400;
    }

    if (path == "/") {
        path = "/index.html";
    }

    std::istringstream parts(path.substr(1));
    std::string part;

    while (std::getline(parts, part, '/')) {
        if (part.empty() || part.front() == '.') {
            return 403;
        }
    }

    return 200;
}

std::string FileHandler::MimeType(const std::string& path) {
    const auto extension = std::filesystem::path(path).extension().string();

    if (extension == ".html") return "text/html; charset=utf-8";

    if (extension == ".css") return "text/css; charset=utf-8";

    if (extension == ".js") return "application/javascript";

    if (extension == ".png") return "image/png";

    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";

    if (extension == ".svg") return "image/svg+xml";

    if (extension == ".txt") return "text/plain; charset=utf-8";

    return "application/octet-stream";
}

int FileHandler::Open(const std::string& target, int& fd, std::uint64_t& size, std::string& type) const {
    std::string path;
    const int status = DecodePath(target, path);

    if (status != 200) {
        return status;
    }

    int directory = dup(_root);

    if (directory == -1) {
        throw std::system_error(errno, std::generic_category(), "dup root");
    }

    std::istringstream parts(path.substr(1));
    std::string part;

    while (std::getline(parts, part, '/')) {
        const bool last = parts.peek() == std::char_traits<char>::eof();
        const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
                          (last ? 0 : O_DIRECTORY);
        const int next = openat(directory, part.c_str(), flags);
        const int error = errno;
        close(directory);

        if (next == -1) {
            if (error == ENOENT) return 404;

            if (error == EACCES || error == EPERM || error == ELOOP || error == ENOTDIR) return 403;

            throw std::system_error(error, std::generic_category(), "open file");
        }

        directory = next;
    }

    struct stat info{};

    if (fstat(directory, &info) == -1) {
        const int error = errno;
        close(directory);
        throw std::system_error(error, std::generic_category(), "fstat");
    }

    if (!S_ISREG(info.st_mode) || path.back() == '/') {
        close(directory);
        return 403;
    }

    type = MimeType(path);
    size = static_cast<std::uint64_t>(info.st_size);
    fd = directory;
    return 200;
}
