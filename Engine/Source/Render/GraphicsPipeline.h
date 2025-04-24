
#pragma once



#include "Render/CommandBuffer.h"
#include <cstdint>
struct GraphicsPipeline
{


    // Uniform buffer management
    virtual void setVertexUniforms(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t slot_index, void *data, uint32_t dataSize)   = 0;
    virtual void setFragmentUniforms(std::shared_ptr<CommandBuffer> commandBuffer, uint32_t slot_index, void *data, uint32_t dataSize) = 0;
};
