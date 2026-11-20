#pragma once
#include "connection.h"
#include "../server/config.h"
#include "../files/file_handler.h"
#include "../logger/logger.h"
#include <signal.h>
#include <map>
#include <sys/select.h>

class Worker {
public:
    /**
     * @brief Создаёт worker; listening socket и журнал принадлежат серверу.
     * @param listen_fd Дескриптор listening socket.
     * @param config Настройки сервера.
     * @param logger Общий журнал событий.
     * @param stop Флаг остановки процесса.
     */
    Worker(int listen_fd, const ServerConfig& config, const Logger& logger,
           const volatile sig_atomic_t& stop);
    /**
     * @brief Закрывает оставшиеся соединения и файлы.
     * @param listen_fd Дескриптор listening socket.
     * @param config Настройки сервера.
     * @param logger Общий журнал событий.
     * @param stop Флаг остановки процесса.
     */
    ~Worker();
    /**
     * @brief Обслуживает клиентов в одном неблокирующем цикле select.
     */
    void Run();
private:
    int _listen_fd;
    const ServerConfig& _config;
    const Logger& _logger;
    const volatile sig_atomic_t& _stop;
    FileHandler _files;
    std::map<int, Connection> _clients;
    /**
     * @brief Собирает наборы сокетов и возвращает максимальный дескриптор.
     * @param readers Набор сокетов для чтения.
     * @param writers Набор сокетов для записи.
     */
    int BuildSets(fd_set& readers, fd_set& writers);
    /**
     * @brief Принимает ограниченную порцию новых клиентов.
     */
    void AcceptConnections();
    /**
     * @brief Читает часть заголовков и запускает подготовку ответа.
     * @param client Состояние соединения клиента.
     */
    void ReadRequest(Connection& client);
    /**
     * @brief Разбирает HTTP и открывает запрошенный файл.
     * @param client Состояние соединения клиента.
     */
    void PrepareResponse(Connection& client);
    /**
     * @brief Формирует короткий ответ об ошибке, учитывая HEAD.
     * @param client Состояние соединения клиента.
     * @param status HTTP-статус ошибки.
     */
    void PrepareError(Connection& client, int status);
    /**
     * @brief Отправляет одну порцию ответа, сохраняя прогресс.
     * @param client Состояние соединения клиента.
     */
    void WriteResponse(Connection& client);
    /**
     * @brief Закрывает ресурсы клиента и записывает результат.
     * @param client Закрываемое соединение.
     * @param reason Причина закрытия для журнала.
     */
    void CloseConnection(Connection& client, const std::string& reason);
    /**
     * @brief Закрывает неактивных клиентов и удаляет завершённые соединения.
     */
    void RemoveClosedConnections();
};
