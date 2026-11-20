#include "http.h"
#include <sstream>
#include <algorithm>
#include <cctype>

int ParseRequest(const std::string& text, HttpRequest& request) {
    const auto end = text.find("\r\n");
    std::istringstream first(text.substr(0, end));
    std::string version, extra;

    if (!(first >> request.method >> request.target >> version) || first >> extra ||
        (version != "HTTP/1.1" && version != "HTTP/1.0") ||
        request.target.empty() || request.target.front() != '/') {
        return 400;
    }

    bool host = false;
    std::size_t pos = end == std::string::npos ? text.size() : end + 2;

    while (pos < text.size()) {
        const auto next = text.find("\r\n", pos);

        if (next == std::string::npos) {
            return 400;
        }

        if (next == pos) {
            break;
        }

        const std::string line = text.substr(pos, next - pos);
        const auto colon = line.find(':');

        if (colon == std::string::npos || colon == 0) {
            return 400;
        }

        std::string name = line.substr(0, colon);

        for (char& ch : name) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-') {
                return 400;
            }

            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }

        if (name == "host") {
            if (host || line.find_first_not_of(" \t", colon + 1) == std::string::npos) {
                return 400;
            }

            host = true;
        }

        pos = next + 2;
    }

    if (version == "HTTP/1.1" && !host) {
        return 400;
    }

    if (request.method != "GET" && request.method != "HEAD") {
        return 405;
    }

    return 200;
}

std::string StatusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default: return "Bad Request";
    }
}

std::string ResponseHeaders(int status, std::uint64_t size, const std::string& type) {
    std::string result = "HTTP/1.1 " + std::to_string(status) + " " + StatusText(status) +
        "\r\nContent-Length: " + std::to_string(size) + "\r\nContent-Type: " + type +
        "\r\nConnection: close\r\n";

    if (status == 405) {
        result += "Allow: GET, HEAD\r\n";
    }

    return result + "\r\n";
}
