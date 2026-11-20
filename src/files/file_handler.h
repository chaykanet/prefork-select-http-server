#pragma once
#include <filesystem>
#include <string>
#include <cstdint>

class FileHandler {
public:
    /**
     * @brief Открывает корневой каталог для безопасного обхода через openat.
     * @param root Корневой каталог статических файлов.
     */
    explicit FileHandler(const std::filesystem::path& root);
    /**
     * @brief Закрывает дескриптор корневого каталога.
     * @param root Корневой каталог статических файлов.
     */
    ~FileHandler();
    FileHandler(const FileHandler&) = delete;
    FileHandler& operator=(const FileHandler&) = delete;
    /**
     * @brief Открывает обычный файл без переходов по симлинкам; fd закрывает вызывающий.
     * @param target Путь из HTTP-запроса.
     * @param fd Выходной дескриптор открытого файла.
     * @param size Выходной размер файла в байтах.
     * @param type Выходной MIME-тип файла.
     */
    int Open(const std::string& target, int& fd, std::uint64_t& size, std::string& type) const;
private:
    int _root;
    /**
     * @brief Декодирует URL и отвергает опасные компоненты пути.
     * @param target Путь из HTTP-запроса.
     * @param path Выходной декодированный путь.
     */
    static int DecodePath(const std::string& target, std::string& path);
    /**
     * @brief Определяет MIME по расширению файла.
     * @param path Путь к файлу.
     */
    static std::string MimeType(const std::string& path);
};
