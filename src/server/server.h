#pragma once
#include "config.h"
#include "../logger/logger.h"
#include <vector>
#include <sys/types.h>

class Server {
public:
    /**
     * @brief Сохраняет настройки сервера.
     * @param config Настройки сервера.
     */
    explicit Server(ServerConfig config);

    /**
     * @brief Запускает prefork-сервер до SIGINT или SIGTERM.
     */
    void ListenAndServe();
private:
    ServerConfig _config;

    /**
     * @brief Проверяет параметры и канонизирует document root.
     */
    bool CheckConfig();

    /**
     * @brief Создаёт и настраивает неблокирующий listening socket.
     */
    int CreateListener();

    /**
     * @brief Создаёт дочерний процесс и вызывает в нём DoWork.
     * @param listener Дескриптор listening socket.
     * @param logger Общий журнал событий.
     */
    pid_t StartWorker(int listener, const Logger& logger);

    /**
     * @brief Запускает цикл обслуживания клиентов в дочернем процессе.
     * @param listener Дескриптор listening socket.
     * @param logger Общий журнал событий.
     */
    void DoWork(int listener, const Logger& logger);

    /**
     * @brief Отслеживает завершение workers и запускает замену.
     * @param listener Дескриптор listening socket.
     * @param workers PID рабочих процессов.
     * @param logger Общий журнал событий.
     */
    void MonitorWorkers(int listener, std::vector<pid_t>& workers, const Logger& logger);

    /**
     * @brief Посылает SIGTERM и собирает завершившиеся дочерние процессы.
     * @param workers PID завершаемых рабочих процессов.
     */
    void StopWorkers(std::vector<pid_t>& workers);
};
