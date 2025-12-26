// Removed unused header <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Model.h"

#include "Core/System/FileSystem.h"
#include "Core/Log.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>


namespace ya
{


void MeshData::createGPUResources()
{
    std::vector<ya::Vertex> vertices;
    for (const auto &v : this->vertices) {
        ya::Vertex vertex;
        vertex.position  = v.position;
        vertex.normal    = v.normal;
        vertex.texCoord0 = v.texCoord;
        vertices.push_back(vertex);
    }

    // Pass source coordinate system to Mesh for proper conversion
    mesh = makeShared<Mesh>(vertices, indices, name, sourceCoordSystem);
}

} // namespace ya