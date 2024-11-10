/**
 * @ Author: godot42
 * @ Create Time: 2024-08-02 10:47:59
 * @ Modified by: @godot42
 * @ Modified time: 2024-11-10 15:35:29
 * @ Description:
 */


#pragma once



#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>


namespace utils
{

struct File
{

    /**
     * Reads the entire contents of a file into a char string
     * @param filepath The path to the file to be read.
     * @return An optional vector of bytes containing the contents of the file.
     *         If the file cannot be opened or read, the optional is empty.
     * @throws None
     */
    static std::optional<std::string> read_all(const std::filesystem::path &filepath);
    // static void                       read_all(const std::filesystem::path &filepath, std::optional<std::string>&  ret);

    static bool is_image(const std::filesystem::path &filepath);


    static std::optional<size_t> get_content_hash(const std::filesystem::path &filepath);
    static std::optional<size_t> get_hash(const std::string &text);

    static std::string get_filename_without_extension(const std::string &path);

    template <class EntryFilter, class Predicate>
    static void foreach_in_folder(std::filesystem::path path, EntryFilter entry_filter, Predicate &&predicate)
    {
        std::vector<std::filesystem::path> entries;
        for (const auto entry : std::filesystem::directory_iterator(path))
        {
            if (entry_filter(entry))
            {
                entries.emplace_back(entry);
            }
        }
        for (const auto &entry : entries) {
            predicate(entry);
        }
    }
};
} // namespace utils