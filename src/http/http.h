#pragma once
#include <string>
#include <cstdint>

struct HttpRequest {
    std::string method;
    std::string target;
};

/**
 * @brief Разбирает заголовки; возвращает 200, 400 или 405.
 * @param text Полные заголовки HTTP-запроса.
 * @param request Результат разбора запроса.
 */
int ParseRequest(const std::string& text, HttpRequest& request);
/**
 * @brief Возвращает текстовое название поддерживаемого статуса.
 * @param status HTTP-статус ответа.
 */
std::string StatusText(int status);
/**
 * @brief Формирует заголовки ответа с закрытием соединения.
 * @param status HTTP-статус ответа.
 * @param size Размер тела ответа в байтах.
 * @param type MIME-тип содержимого.
 */
std::string ResponseHeaders(int status, std::uint64_t size, const std::string& type);
