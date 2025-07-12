// Removed unused header <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Model.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>



void Model::draw(SDL_GPURenderPass *renderPass, SDL_GPUTexture *defaultTexture)
{
    // This function would be called if we had direct access to GPU buffers
    // In our current setup, we'll refactor Entry.cpp to handle the drawing
    // This is just a placeholder for potential future improvements
}
