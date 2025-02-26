#pragma once

#include <filesystem>

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
