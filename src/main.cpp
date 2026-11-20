#include "server/server.h"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

/**
 * @brief Читает положительное целое число в заданных границах.
 * @param text Строка с числом.
 * @param maximum Максимально допустимое значение.
 */
unsigned long long ReadNumber(std::string_view text, unsigned long long maximum) {
    unsigned long long value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);

    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 || value > maximum) {
        throw std::invalid_argument("invalid numeric option");
    }

    return value;
}

/**
 * @brief Разбирает параметры запуска без сторонних библиотек.
 * @param argc Количество аргументов.
 * @param argv Аргументы командной строки.
 */
ServerConfig ReadConfig(int argc, char** argv) {
    ServerConfig config;

    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            throw std::invalid_argument("option requires a value");
        }

        const std::string option = argv[i];
        const std::string value = argv[i + 1];

        if (option == "--host") config.host = value;

        else if (option == "--port") config.port = static_cast<std::uint16_t>(ReadNumber(value, 65535));

        else if (option == "--root") config.document_root = value;

        else if (option == "--workers") config.workers = ReadNumber(value, 256);

        else if (option == "--max-connections") config.max_connections_per_worker = ReadNumber(value, 65535);

        else if (option == "--timeout") config.client_timeout = std::chrono::seconds(ReadNumber(value, 86400));

        else if (option == "--log") config.log_path = value;

        else throw std::invalid_argument("unknown option: " + option);
    }

    return config;
}

/**
 * @brief Запускает сервер и возвращает ненулевой код при ошибке.
 * @param argc Количество аргументов.
 * @param argv Аргументы командной строки.
 */
int main(int argc, char** argv) {
    try {
        Server server(ReadConfig(argc, argv));
        server.ListenAndServe();
    }

    catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
