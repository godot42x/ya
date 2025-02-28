#pragma once

#include <filesystem>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_storage.h"
#include "SDL3/SDL_timer.h"


inline SDL_Storage *openFileStorage(const char *dirPath, bool bSync)
{
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
    }
    auto ret = SDL_OpenFileStorage(dirPath);
    if (bSync) {
        while (!SDL_StorageReady(ret)) {
            SDL_Delay(1);
        }
    }
    return ret;
}


inline std::optional<std::vector<char>> readStorageFile(SDL_Storage *storage, std::string_view fileName)
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
