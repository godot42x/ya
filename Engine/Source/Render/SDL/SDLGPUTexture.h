#pragma once

#include "Render/Texture.h"
#include "SDL3/SDL_gpu.h"

struct GPUTexture_SDL : public Texture
{
    SDL_GPUTexture *texture = nullptr;

    GPUTexture_SDL(SDL_GPUTexture *texture)
    {
    }
};
