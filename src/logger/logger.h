#pragma once
#include <filesystem>
#include <string>

class Logger {
public:

    /**
     * @brief Открывает общий журнал в режиме добавления.
     * @param path Путь к файлу журнала.
     */
    explicit Logger(const std::filesystem::path& path);

    /**
     * @brief Закрывает журнал.
     * @param path Путь к файлу журнала.
     */
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief Записывает короткую строку с временем и PID одной операцией write.
     * @param message Текст записываемого события.
     */
    void Write(const std::string& message) const;
private:
    int _fd;
};
