#include "worker.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>
#include <algorithm>

Worker::Worker(int listen_fd, const ServerConfig& config, const Logger& logger, const volatile sig_atomic_t& stop) :
        _listen_fd(listen_fd), _config(config), _logger(logger), _stop(stop),
        _files(config.document_root) {}

Worker::~Worker() {
    for (auto& [fd, client] : _clients) {
        if (client.file >= 0) close(client.file);

        if (!client.closed) close(fd);
    }
}

int Worker::BuildSets(fd_set& readers, fd_set& writers) {
    FD_ZERO(&readers);
    FD_ZERO(&writers);
    int maximum = _listen_fd;

    if (_clients.size() < _config.max_connections_per_worker) {
        FD_SET(_listen_fd, &readers);
    }

    for (auto& [fd, client] : _clients) {

        if (client.state == ConnectionState::ReadingRequest) {
            FD_SET(fd, &readers);
        }

        else {
            FD_SET(fd, &writers);
        }

        maximum = std::max(maximum, fd);
    }

    return maximum;
}

void Worker::Run() {
    if (_listen_fd < 0 || _listen_fd >= FD_SETSIZE) {
        throw std::runtime_error("invalid listening descriptor");
    }

    while (!_stop) {
        fd_set readers, writers;
        const int maximum = BuildSets(readers, writers);
        timeval timeout{};
        timeout.tv_sec = 1;
        const int ready = select(maximum + 1, &readers, &writers, nullptr, &timeout);

        if (ready == -1) {
            if (errno == EINTR) continue;

            throw std::system_error(errno, std::generic_category(), "select");
        }

        if (FD_ISSET(_listen_fd, &readers)) {
            AcceptConnections();
        }

        for (auto& [fd, client] : _clients) {
            try {
                if (FD_ISSET(fd, &readers)) ReadRequest(client);

                if (!client.closed && FD_ISSET(fd, &writers)) WriteResponse(client);
            }

            catch (const std::exception& error) {
                CloseConnection(client, error.what());
            }
        }

        RemoveClosedConnections();
    }

    for (auto& [fd, client] : _clients) {
        CloseConnection(client, "shutdown");
    }
}

void Worker::AcceptConnections() {
    for (int count = 0; count < 32 && !_stop &&
         _clients.size() < _config.max_connections_per_worker; ++count) {
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        const int fd = accept(_listen_fd, reinterpret_cast<sockaddr*>(&address), &length);

        if (fd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                _logger.Write("accept error=" + std::to_string(errno));
            }

            return;
        }

        const int flags = fcntl(fd, F_GETFL, 0);

        if (fd >= FD_SETSIZE || flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(fd);
            continue;
        }

        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));

        try {
            auto& client = _clients[fd];
            client.socket = fd;
            client.ip = ip;
        }

        catch (...) {
            close(fd);
            _clients.erase(fd);
            throw;
        }
    }
}

void Worker::ReadRequest(Connection& client) {

    char buffer[4096];
    const auto received = recv(client.socket, buffer, sizeof(buffer), 0);

    if (received == 0) {
        CloseConnection(client, "client closed");
        return;
    }

    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            CloseConnection(client, "recv error=" + std::to_string(errno));
        }

        return;
    }

    client.activity = std::chrono::steady_clock::now();
    client.input.append(buffer, static_cast<std::size_t>(received));
    const auto end = client.input.find("\r\n\r\n");

    if ((end == std::string::npos && client.input.size() > 16384) ||
        (end != std::string::npos && end + 4 > 16384)) {
        client.request.method = client.input.substr(0, client.input.find(' '));
        PrepareError(client, 400);
        return;
    }

    if (end != std::string::npos) {
        client.input.resize(end + 4);
        PrepareResponse(client);
    }
}

void Worker::PrepareResponse(Connection& client) {
    const int parsed = ParseRequest(client.input, client.request);

    if (parsed != 200) {
        PrepareError(client, parsed);
        return;
    }

    std::string type;
    std::uint64_t size = 0;
    const int status = _files.Open(client.request.target, client.file, size, type);

    if (status != 200) {
        PrepareError(client, status);
        return;
    }

    client.status = 200;
    client.output = ResponseHeaders(200, size, type);
    client.remaining = client.request.method == "HEAD" ? 0 : size;
    client.state = ConnectionState::WritingResponse;
}

void Worker::PrepareError(Connection& client, int status) {
    const std::string body = std::to_string(status) + " " + StatusText(status) + "\n";
    client.status = status;
    client.output = ResponseHeaders(status, body.size(), "text/plain; charset=utf-8");

    if (client.request.method != "HEAD") {
        client.output += body;
    }

    client.state = ConnectionState::WritingResponse;
}

void Worker::WriteResponse(Connection& client) {
    if (client.offset == client.output.size()) {
        if (client.remaining == 0) {
            CloseConnection(client, "complete");
            return;
        }

        char buffer[65536];
        const auto count = read(client.file, buffer,
            static_cast<std::size_t>(std::min<std::uint64_t>(sizeof(buffer), client.remaining)));

        if (count < 0 && errno == EINTR) return;

        if (count <= 0) {
            CloseConnection(client, "file read failed or file changed");
            return;
        }

        client.output.assign(buffer, static_cast<std::size_t>(count));
        client.offset = 0;
        client.remaining -= static_cast<std::uint64_t>(count);
    }

    const auto sent = send(client.socket, client.output.data() + client.offset, client.output.size() - client.offset, 0);

    if (sent < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            CloseConnection(client, "send error=" + std::to_string(errno));
        }

        return;
    }

    client.offset += static_cast<std::size_t>(sent);
    client.sent += static_cast<std::uint64_t>(sent);
    client.activity = std::chrono::steady_clock::now();

    if (client.offset == client.output.size() && client.remaining == 0) {
        CloseConnection(client, "complete");
    }
}

void Worker::CloseConnection(Connection& client, const std::string& reason) {
    if (client.closed) return;

    close(client.socket);

    if (client.file >= 0) {
        close(client.file);
        client.file = -1;
    }

    client.closed = true;
    _logger.Write("ip=" + client.ip + " method=" + client.request.method +
        " path=" + client.request.target + " status=" + std::to_string(client.status) +
        " bytes=" + std::to_string(client.sent) + " result=" + reason);
}

void Worker::RemoveClosedConnections() {
    const auto now = std::chrono::steady_clock::now();

    for (auto it = _clients.begin(); it != _clients.end();) {
        auto& client = it->second;

        if (!client.closed && now - client.activity >= _config.client_timeout) {
            CloseConnection(client, "timeout");
        }

        if (client.closed) {
            it = _clients.erase(it);
        }

        else {
            ++it;
        }
    }
}
