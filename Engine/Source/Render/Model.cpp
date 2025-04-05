#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Model.h"


#include "Core/FileSystem/FileSystem.h"
#include "Core/Log.h"
#include "Render/CommandBuffer.h"


bool Model::loadFromOBJ(const std::string &filePath, std::shared_ptr<CommandBuffer> commandBuffer)

{
    m_meshes.clear();
    m_isLoaded  = false;
    m_directory = std::filesystem::path(filePath).parent_path().string();

    std::string fileContent;
    if (!FileSystem::get()->readFileToString(filePath, fileContent)) {
        NE_CORE_ERROR("Failed to read OBJ file: {}", filePath);
        return false;
    }

    std::istringstream stream(fileContent);
    std::string        line;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;

    Mesh currentMesh;
    currentMesh.name = std::filesystem::path(filePath).filename().string();

    while (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string        prefix;
        lineStream >> prefix;

        if (prefix == "v") {
            // Position
            glm::vec3 position;
            lineStream >> position.x >> position.y >> position.z;
            positions.push_back(position);
        }
        else if (prefix == "vn") {
            // Normal
            glm::vec3 normal;
            lineStream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        }
        else if (prefix == "vt") {
            // Texture coordinate
            glm::vec2 texCoord;
            lineStream >> texCoord.x >> texCoord.y;
            texCoord.y = 1.0f - texCoord.y; // Flip Y for OpenGL/Vulkan
            texCoords.push_back(texCoord);
        }
        else if (prefix == "f") {
            // Face
            std::string           vertexStr;
            std::vector<uint32_t> faceIndices;

            while (lineStream >> vertexStr) {
                // Format could be: v, v/vt, v/vt/vn, v//vn
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                std::istringstream vertexStream(vertexStr);

                int posIndex = 0, texIndex = 0, normIndex = 0;
                vertexStream >> posIndex;

                if (vertexStream.peek() != EOF) {
                    vertexStream >> texIndex;
                }

                if (vertexStream.peek() != EOF) {
                    vertexStream >> normIndex;
                }

                // OBJ indices are 1-based, convert to 0-based
                posIndex  = posIndex > 0 ? posIndex - 1 : posIndex + positions.size();
                texIndex  = texIndex > 0 ? texIndex - 1 : texIndex + texCoords.size();
                normIndex = normIndex > 0 ? normIndex - 1 : normIndex + normals.size();

                // Try to find an existing vertex
                bool     foundVertex   = false;
                uint32_t existingIndex = 0;

                for (size_t i = 0; i < currentMesh.vertices.size(); ++i) {
                    Vertex &v = currentMesh.vertices[i];

                    bool posMatch  = posIndex < positions.size() && v.position == positions[posIndex];
                    bool normMatch = normIndex < normals.size() && v.normal == normals[normIndex];
                    bool texMatch  = texIndex < texCoords.size() && v.texCoord == texCoords[texIndex];

                    if (posMatch && normMatch && texMatch) {
                        foundVertex   = true;
                        existingIndex = static_cast<uint32_t>(i);
                        break;
                    }
                }

                if (foundVertex) {
                    faceIndices.push_back(existingIndex);
                }
                else {
                    Vertex vertex;

                    if (posIndex < positions.size()) {
                        vertex.position = positions[posIndex];
                    }

                    if (normIndex < normals.size()) {
                        vertex.normal = normals[normIndex];
                    }

                    if (texIndex < texCoords.size()) {
                        vertex.texCoord = texCoords[texIndex];
                    }

                    currentMesh.vertices.push_back(vertex);
                    faceIndices.push_back(static_cast<uint32_t>(currentMesh.vertices.size() - 1));
                }
            }

            // Triangulate the face if it has more than 3 vertices
            if (faceIndices.size() >= 3) {
                for (size_t i = 2; i < faceIndices.size(); ++i) {
                    currentMesh.indices.push_back(faceIndices[0]);
                    currentMesh.indices.push_back(faceIndices[i - 1]);
                    currentMesh.indices.push_back(faceIndices[i]);
                }
            }
        }
        else if (prefix == "mtllib") {
            // Material library - could be implemented for more complex models
            std::string mtlFileName;
            lineStream >> mtlFileName;
            NE_CORE_INFO("Material file referenced: {}", mtlFileName);
        }
        else if (prefix == "usemtl") {
            // Material usage - could be implemented for more complex models
            std::string materialName;
            lineStream >> materialName;
            NE_CORE_INFO("Material referenced: {}", materialName);
        }
    }

    // If we have a mesh with vertices, add it
    if (!currentMesh.vertices.empty()) {
        NE_CORE_INFO("Loaded mesh '{}' with {} vertices and {} indices",
                     currentMesh.name,
                     currentMesh.vertices.size(),
                     currentMesh.indices.size());

        // Check for texture file with the same base name as the OBJ
        std::string              baseFileName      = std::filesystem::path(filePath).stem().string();
        std::vector<std::string> potentialTextures = {
            m_directory + "/" + baseFileName + ".png",
            m_directory + "/" + baseFileName + ".jpg",
            m_directory + "/" + baseFileName + ".jpeg",
            m_directory + "/" + baseFileName + ".bmp"};

        for (const auto &texturePath : potentialTextures) {
            if (FileSystem::get()->isFileExists(texturePath)) {
                // Use the command buffer to create texture
                currentMesh.diffuseTexture = commandBuffer->createTexture(texturePath);
                if (currentMesh.diffuseTexture) {
                    NE_CORE_INFO("Loaded texture for mesh: {}", texturePath);
                    break;
                }
            }
        }

        m_meshes.push_back(currentMesh);
    }

    m_isLoaded = true;
    return m_isLoaded;
}

void Model::draw(SDL_GPURenderPass *renderPass, SDL_GPUTexture *defaultTexture)
{
    // This function would be called if we had direct access to GPU buffers
    // In our current setup, we'll refactor Entry.cpp to handle the drawing
    // This is just a placeholder for potential future improvements
}
