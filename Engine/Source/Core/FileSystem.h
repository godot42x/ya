#pragma once

#include <filesystem>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_storage.h"
#include "SDL3/SDL_timer.h"


struct DirectoryStore
{
    SDL_Storage *storage;
    std::string  dirPath;

    inline void create(const char *dirPath, bool bSync)
    {
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
        storage = SDL_OpenFileStorage(dirPath);
        if (bSync) {
            while (!SDL_StorageReady(storage)) {
                SDL_Delay(1);
            }
        }
    }

    void close() { SDL_CloseStorage(storage); }


    std::optional<std::vector<char>> readStorageFile(std::string_view fileName)
    {
        std::size_t size;
        if (!SDL_GetStorageFileSize(storage, fileName.data(), &size)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to get file size of %s: %s", fileName.data(), SDL_GetError());
            return {};
        }
        std::vector<char> content(size);
        if (!SDL_ReadStorageFile(storage, fileName.data(), content.data(), content.size())) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to read file %s: %s", fileName.data(), SDL_GetError());
            return {};
        }

        return content;
    }

    std::string getFullPath(std::string_view fileName)
    {
        return (std::filesystem::path(dirPath) / fileName).string();
    }
};
