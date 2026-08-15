// #include "OpenGLDescriptorSet.h"
// #include "Core/Log.h"
// #include "OpenGLRender.h"

// namespace ya
// {

// bool OpenGLDescriptorPool::allocateDescriptorSets(
//     const std::shared_ptr<IDescriptorSetLayout> &layout,
//     uint32_t                                     count,
//     std::vector<DescriptorSetHandle>            &outSets)
// {
//     outSets.clear();
//     outSets.reserve(count);

//     // In OpenGL, we just create lightweight handles
//     for (uint32_t i = 0; i < count; ++i) {
//         // Create a unique pointer as a handle
//         void *handle = reinterpret_cast<void *>(static_cast<uintptr_t>(_allocatedSets + i + 1));
//         outSets.push_back(DescriptorSetHandle(handle));
//     }

//     _allocatedSets += count;
//     return true;
// }

// void OpenGLDescriptorHelper::updateDescriptorSets(
//     const std::vector<WriteDescriptorSet> &writes,
//     const std::vector<CopyDescriptorSet>  &copies)
// {
//     // Process write operations
//     for (const auto &write : writes) {
//         switch (write.descriptorType) {
//         case EPipelineDescriptorType::UniformBuffer:
//         case EPipelineDescriptorType::StorageBuffer:
//         case EPipelineDescriptorType::UniformBufferDynamic:
//         case EPipelineDescriptorType::StorageBufferDynamic:
//             applyBufferWrite(write);
//             break;

//         case EPipelineDescriptorType::CombinedImageSampler:
//         case EPipelineDescriptorType::SampledImage:
//         case EPipelineDescriptorType::StorageImage:
//             applyImageWrite(write);
//             break;

//         default:
//             YA_CORE_WARN("Unsupported descriptor type in OpenGL: {}", static_cast<int>(write.descriptorType));
//             break;
//         }
//     }

//     // Process copy operations (if needed)
//     for (const auto &copy : copies) {
//         YA_CORE_WARN("OpenGL descriptor set copies not yet implemented");
//     }
// }

// void OpenGLDescriptorHelper::applyBufferWrite(const WriteDescriptorSet &write)
// {
//     if (!write.pBufferInfo) {
//         YA_CORE_ERROR("OpenGLDescriptorHelper::applyBufferWrite - buffer info is null");
//         return;
//     }

//     // Get or create descriptor set data
//     void *setHandle = write.dstSet.as<void *>();
//     auto &setData   = _descriptorSets[setHandle];

//     // Ensure buffer bindings vector is large enough
//     uint32_t bindingIndex = write.dstBinding + write.dstArrayElement;
//     if (setData.bufferBindings.size() <= bindingIndex) {
//         setData.bufferBindings.resize(bindingIndex + 1, 0);
//     }

//     // Store the buffer handle
//     const DescriptorBufferInfo &bufferInfo = *write.pBufferInfo;
//     GLuint                      buffer     = static_cast<GLuint>(reinterpret_cast<uintptr_t>(bufferInfo.buffer.as<void *>()));
//     setData.bufferBindings[bindingIndex]   = buffer;

//     // Bind the buffer to its binding point
//     GLenum target = (write.descriptorType == EPipelineDescriptorType::UniformBuffer ||
//                      write.descriptorType == EPipelineDescriptorType::UniformBufferDynamic)
//                         ? GL_UNIFORM_BUFFER
//                         : GL_SHADER_STORAGE_BUFFER;

//     glBindBufferRange(target, bindingIndex, buffer, bufferInfo.offset, bufferInfo.range);
// }

// void OpenGLDescriptorHelper::applyImageWrite(const WriteDescriptorSet &write)
// {
//     if (!write.pImageInfo) {
//         YA_CORE_ERROR("OpenGLDescriptorHelper::applyImageWrite - image info is null");
//         return;
//     }

//     // Get or create descriptor set data
//     void *setHandle = write.dstSet.as<void *>();
//     auto &setData   = _descriptorSets[setHandle];

//     // Ensure texture bindings vector is large enough
//     uint32_t bindingIndex = write.dstBinding + write.dstArrayElement;
//     if (setData.textureBindings.size() <= bindingIndex) {
//         setData.textureBindings.resize(bindingIndex + 1, 0);
//     }

//     // Store the texture handle
//     const DescriptorImageInfo &imageInfo = *write.pImageInfo;
//     GLuint                     texture   = static_cast<GLuint>(reinterpret_cast<uintptr_t>(imageInfo.imageView.as<void *>()));
//     setData.textureBindings[bindingIndex] = texture;

//     // Bind the texture to its texture unit
//     glActiveTexture(GL_TEXTURE0 + bindingIndex);
//     glBindTexture(GL_TEXTURE_2D, texture);

//     // Bind the sampler if provided
//     if (imageInfo.sampler.as<void *>() != nullptr) {
//         GLuint sampler = static_cast<GLuint>(reinterpret_cast<uintptr_t>(imageInfo.sampler.as<void *>()));
//         glBindSampler(bindingIndex, sampler);
//     }
// }

// void OpenGLDescriptorHelper::bindDescriptorSet(DescriptorSetHandle descriptorSet, GLuint program)
// {
//     void *setHandle = descriptorSet.as<void *>();
//     auto  it        = _descriptorSets.find(setHandle);

//     if (it == _descriptorSets.end()) {
//         YA_CORE_WARN("Descriptor set not found: {}", setHandle);
//         return;
//     }

//     const auto &setData = it->second;

//     // Bind uniform buffers
//     for (size_t i = 0; i < setData.bufferBindings.size(); ++i) {
//         if (setData.bufferBindings[i] != 0) {
//             // The buffer is already bound via glBindBufferRange in applyBufferWrite
//             // Just ensure the uniform block index is correct
//             GLuint blockIndex = glGetUniformBlockIndex(program, ("Binding" + std::to_string(i)).c_str());
//             if (blockIndex != GL_INVALID_INDEX) {
//                 glUniformBlockBinding(program, blockIndex, static_cast<GLuint>(i));
//             }
//         }
//     }

//     // Bind textures
//     for (size_t i = 0; i < setData.textureBindings.size(); ++i) {
//         if (setData.textureBindings[i] != 0) {
//             glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
//             glBindTexture(GL_TEXTURE_2D, setData.textureBindings[i]);
//         }
//     }
// }

// } // namespace ya
