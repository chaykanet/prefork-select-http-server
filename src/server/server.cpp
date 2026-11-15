#include "server.h"

#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <system_error>
#include <utility>
#include <stdexcept>
#include <vector>

namespace {

volatile sig_atomic_t stop_requested = 0;

void HandleStopSignal(int) {
    stop_requested = 1;
}

void RegisterSignalHandlers() {
    struct sigaction stop_action {};
    stop_action.sa_handler = HandleStopSignal;
    sigemptyset(&stop_action.sa_mask);
    stop_action.sa_flags = 0;

    for (const int signal_number : {SIGINT, SIGTERM}) {
        if (sigaction(signal_number, &stop_action, nullptr) == -1) {
            throw std::system_error(errno, std::generic_category(), "cannot register shutdown signal handler");
        }
    }

    struct sigaction pipe_action {};
    sigemptyset(&pipe_action.sa_mask);
    pipe_action.sa_handler = SIG_IGN;
    
    if (sigaction(SIGPIPE, &pipe_action, nullptr) == -1) {
        throw std::system_error(errno, std::generic_category(), "cannot ignore SIGPIPE");
    }
}

}

Server::Server(ServerConfig config): _config(std::move(config)) {}

bool Server::CheckConfig() {
    
    const auto fail = [](const std::string& message) {
        std::cerr << "Invalid server configuration: " << message << '\n';
        return false;
    };

    in_addr address{};
    if (inet_pton(AF_INET, _config.host.c_str(), &address) != 1) {
        return fail("host must be an IPv4 address, for example 127.0.0.1");
    }
    
    if (_config.port == 0) {
        return fail("port must be between 1 and 65535");
    }
    
    if (_config.workers == 0) {
        return fail("workers must be greater than zero");
    }
    
    if (_config.max_connections_per_worker == 0 || _config.max_connections_per_worker >= static_cast<std::size_t>(FD_SETSIZE)) {
        return fail("max_connections_per_worker must be between 1 and " + std::to_string(FD_SETSIZE - 1));
    }
    
    if (_config.client_timeout.count() <= 0) {
        return fail("client_timeout must be positive");
    }

    std::error_code error;
    const auto root = std::filesystem::canonical(_config.document_root, error);
    if (error) {
        return fail("cannot resolve document_root: " + error.message());
    }
    
    if (!std::filesystem::is_directory(root, error) || error) {
        return fail("document_root must be an existing directory");
    }

    if (_config.log_path.empty() || _config.log_path.filename().empty()) {
        return fail("log_path must name a file");
    }
    
    const auto log_parent = _config.log_path.has_parent_path()
        ? _config.log_path.parent_path() : std::filesystem::path(".");
    
        if (!std::filesystem::is_directory(log_parent, error) || error) {
        return fail("the parent directory of log_path must exist");
    }
    
    const bool log_exists = std::filesystem::exists(_config.log_path, error);
    if (error) {
        return fail("cannot inspect log_path: " + error.message());
    }
    
    if (log_exists &&
        (!std::filesystem::is_regular_file(_config.log_path, error) || error)) {
        return fail("log_path must refer to a regular file");
    }

    _config.document_root = root;
    return true;
}

void Server::ListenAndServe() {
    if (!CheckConfig()) {
        throw std::invalid_argument("invalid server configuration");
    }

    stop_requested = 0;
    RegisterSignalHandlers();

    std::vector<pid_t> workers(_config.workers, -1);
    const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        throw std::system_error(errno, std::generic_category(), "socket");
    }

    const auto stop_workers = [&workers]() {
        for (const pid_t pid : workers) {
            if (pid > 0) {
                kill(pid, SIGTERM);
            }
        }

        for (pid_t& pid : workers) {
            if (pid > 0) {
                while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {}
                pid = -1;
            }
        }
    };

    const auto start_worker = [this, listen_fd]() -> pid_t {
        const pid_t pid = fork();
        
        if (pid == -1) {
            throw std::system_error(errno, std::generic_category(), "fork");
        }
        
        if (pid == 0) {
            int exit_code = 0;
            try {
                DoWork(listen_fd);
            } 
            catch (const std::exception& error) {
                std::cerr << "Worker " << getpid() << ": " << error.what() << '\n';
                exit_code = 1;
            } 
            catch (...) {
                exit_code = 1;
            }

            close(listen_fd);
            _exit(exit_code);
        }
        return pid;
    };

    try {
        
        if (listen_fd >= FD_SETSIZE) {
            throw std::runtime_error("listening socket exceeds select descriptor limit");
        }
        
        const int reuse_address = 1;
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                       &reuse_address, sizeof(reuse_address)) == -1) {
            throw std::system_error(errno, std::generic_category(), "setsockopt");
        }
        
        const int flags = fcntl(listen_fd, F_GETFL, 0);
        if (flags == -1 || fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::system_error(errno, std::generic_category(), "fcntl");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(_config.port);
        inet_pton(AF_INET, _config.host.c_str(), &address.sin_addr);
        if (bind(listen_fd, reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) == -1) {
            throw std::system_error(errno, std::generic_category(), "bind");
        }
        
        if (listen(listen_fd, SOMAXCONN) == -1) {
            throw std::system_error(errno, std::generic_category(), "listen");
        }

        for (pid_t& pid : workers) {
            if (stop_requested) {
                break;
            }
            pid = start_worker();
        }

        while (!stop_requested) {
           
            for (pid_t& pid : workers) {
                if (stop_requested) {
                    break;
                }
                
                const pid_t result = waitpid(pid, nullptr, WNOHANG);
                if (result == pid) {
                    pid = -1;
                    if (!stop_requested) {
                        pid = start_worker();
                    }
                } 
                else if (result == -1 && errno != EINTR) {
                    if (errno == ECHILD) {
                        pid = -1;
                    }
                    throw std::system_error(errno, std::generic_category(), "waitpid");
                }
            }
            
            timeval timeout{};
            timeout.tv_usec = 100000;
            if (!stop_requested &&
                select(0, nullptr, nullptr, nullptr, &timeout) == -1 && errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "master wait");
            }
        }
    } catch (...) {
        close(listen_fd);
        stop_workers();
        throw;
    }

    close(listen_fd);
    stop_workers();
}

void Server::DoWork(int listen_fd) {
    (void) listen_fd;
    
    while (!stop_requested) {
        timeval timeout{};
        timeout.tv_sec = 1;
        if (select(0, nullptr, nullptr, nullptr, &timeout) == -1 && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "worker wait");
        }
    }
}
