#include "CommandBuffer.h"

void CommandBuffer::uploadBuffers(const void *vertexData, uint32_t vertexDataSize, const void *indexData, uint32_t indexDataSize)
{
    uploadVertexBuffers(vertexData, vertexDataSize);
    uploadIndexBuffers(indexData, indexDataSize);
}
