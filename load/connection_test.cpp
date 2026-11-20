#include <arpa/inet.h>
#include <signal.h>
#include <cerrno>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Result {
    std::uint64_t bytes = 0;
    double seconds = 0;
    std::string status = "error";
};

struct StartGate {
    std::mutex mutex;
    std::condition_variable changed;
    int ready = 0;
    bool start = false;
    Clock::time_point time;
};

/**
 * @brief Принимает ответ, проверяет статус и размер тела.
 * @param fd Сокет клиента.
 * @param result Результат измерения.
 */
void Receive(int fd, Result& result) {
    std::string headers;
    bool parsed = false;
    std::uint64_t expected = 0;
    char buffer[65536];

    while (true) {
        const auto count = recv(fd, buffer, sizeof(buffer), 0);

        if (count == 0) break;

        if (count < 0) {
            if (errno == EINTR) continue;

            return;
        }

        if (parsed) {
            result.bytes += static_cast<std::uint64_t>(count);
            continue;
        }
        headers.append(buffer, static_cast<std::size_t>(count));
        const auto end = headers.find("\r\n\r\n");

        if (end == std::string::npos) {
            if (headers.size() > 16384) return;

            continue;
        }

        if (!headers.starts_with("HTTP/1.1 200 OK\r\n")) return;

        const auto length = headers.find("\r\nContent-Length: ");

        if (length == std::string::npos || length > end) return;

        std::istringstream value(headers.substr(length + 18));

        if (!(value >> expected)) return;

        result.bytes = headers.size() - end - 4;
        parsed = true;
    }

    if (parsed && result.bytes == expected) result.status = "ok";
}

/**
 * @brief Подключается, ожидает общий старт и скачивает один файл.
 * @param port Порт локального сервера.
 * @param gate Барьер старта всех клиентов.
 * @param result Результат данного соединения.
 */
void Download(int port, StartGate& gate, Result& result) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    timeval timeout{};
    timeout.tv_sec = 30;

    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    const bool connected = fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        ++gate.ready;
        gate.changed.notify_all();
        gate.changed.wait(lock, [&gate] { return gate.start; });
    }

    if (connected) {
        const std::string request = "GET /payload.bin HTTP/1.1\r\nHost: localhost\r\n\r\n";
        std::size_t offset = 0;

        while (offset < request.size()) {
            const auto count = send(fd, request.data() + offset, request.size() - offset, 0);

            if (count < 0 && errno == EINTR) continue;

            if (count <= 0) break;

            offset += static_cast<std::size_t>(count);
        }

        if (offset == request.size()) Receive(fd, result);
    }
    result.seconds = std::chrono::duration<double>(Clock::now() - gate.time).count();

    if (fd >= 0) close(fd);
}

/**
 * @brief Запускает клиентов и сохраняет CSV и краткий итог.
 * @param argc Количество аргументов.
 * @param argv Порт, число соединений и выходной CSV.
 */
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: connection_test PORT CONNECTIONS OUTPUT.csv\n";
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    const int port = std::stoi(argv[1]);
    const int count = std::stoi(argv[2]);

    if (port < 1 || port > 65535 || count < 1 || count > 1000) return 1;

    std::vector<Result> results(count);
    std::vector<std::thread> threads;
    StartGate gate;

    try {
        for (auto& result : results) {
            threads.emplace_back(Download, port, std::ref(gate), std::ref(result));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        {
            std::lock_guard<std::mutex> lock(gate.mutex);
            gate.time = Clock::now();
            gate.start = true;
        }
        gate.changed.notify_all();

        for (auto& thread : threads) thread.join();

        return 1;
    }
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        gate.changed.wait(lock, [&gate, count] { return gate.ready == count; });
        gate.time = Clock::now();
        gate.start = true;
    }
    gate.changed.notify_all();

    for (auto& thread : threads) thread.join();

    const double elapsed = std::chrono::duration<double>(Clock::now() - gate.time).count();
    std::ofstream csv(argv[3]);

    if (!csv) return 1;

    csv << "connection_id,bytes_received,seconds,MiB_per_s,status\n" << std::fixed << std::setprecision(6);
    std::uint64_t bytes = 0;
    std::vector<double> speeds;

    for (int i = 0; i < count; ++i) {
        const auto& result = results[i];
        const double speed = result.bytes / 1048576.0 / result.seconds;
        csv << i + 1 << ',' << result.bytes << ',' << result.seconds << ',' << speed << ',' << result.status << '\n';

        if (result.status == "ok") {
            bytes += result.bytes;
            speeds.push_back(speed);
        }
    }
    std::sort(speeds.begin(), speeds.end());
    std::cout << "Соединений: " << count << "\nУспешных: " << speeds.size()
              << "\nОшибок: " << count - speeds.size() << "\nВремя, с: " << elapsed
              << "\nСуммарная скорость успешных передач, МиБ/с: " << bytes / 1048576.0 / elapsed << '\n';

    if (!speeds.empty()) {
        const auto n = speeds.size();
        std::cout << "Минимальная скорость, МиБ/с: " << speeds.front()
                  << "\nМедианная скорость, МиБ/с: " << (speeds[(n - 1) / 2] + speeds[n / 2]) / 2
                  << "\np95 скорости, МиБ/с: " << speeds[(95 * n + 99) / 100 - 1] << '\n';
    }
    return speeds.size() == results.size() && csv.good() ? 0 : 1;
}
