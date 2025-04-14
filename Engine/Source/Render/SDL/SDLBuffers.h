#pragma once
#include "Core/Log.h"
#include "SDL3/SDL_gpu.h"
#include <functional>
#include <memory>
#include <string>

// Forward declarations
class SDLGPUBuffer;
class SDLGPUTransferBuffer;

// Typedefs for smart pointers
using SDLGPUBufferPtr         = std::shared_ptr<SDLGPUBuffer>;
using SDLGPUTransferBufferPtr = std::shared_ptr<SDLGPUTransferBuffer>;

// RAII wrapper for SDL_GPUBuffer with self-contained size tracking
class SDLGPUBuffer
{
    // Disallow copying
    SDLGPUBuffer(const SDLGPUBuffer &)            = delete;
    SDLGPUBuffer &operator=(const SDLGPUBuffer &) = delete;

    SDL_GPUDevice &m_device; // Reference to ensure device outlives buffer
    SDL_GPUBuffer *m_buffer;
    size_t         m_size;
    std::string    m_name;

  private:
    // Private constructor to ensure creation through factory method
    SDLGPUBuffer(SDL_GPUDevice &device)
        : m_device(device) {}

  public:
    enum class Usage
    {
        VertexBuffer,
        IndexBuffer,
        // Add other usages as needed
    };

    ~SDLGPUBuffer()
    {
        if (m_buffer) {
            SDL_ReleaseGPUBuffer(&m_device, m_buffer);
        }
        m_buffer = nullptr;
    }

    SDL_GPUBuffer *getBuffer() const { return m_buffer; }
    size_t         getSize() const { return m_size; }

    // Static factory method
    static SDLGPUBufferPtr Create(SDL_GPUDevice &device, const std::string &name, Usage usage, size_t size)
    {
        auto ptr = std::shared_ptr<SDLGPUBuffer>(new SDLGPUBuffer(device));
        ptr->createInternal(size, usage, name);
        return ptr;
    }

    // Method to recreate buffer with larger size if needed
    void tryExtendSize(const std::string &name, Usage usage, size_t requiredSize)
    {
        if (requiredSize <= m_size) {
            return;
        }

        // Calculate new size (double the current size or use required size if larger)
        size_t newSize = m_size * 2;
        if (newSize < requiredSize) {
            newSize = requiredSize;
        }

        SDL_ReleaseGPUBuffer(&m_device, m_buffer);
        m_buffer = nullptr;
        createInternal(newSize, usage, name);
    }

  private:
    void createInternal(std::size_t size, Usage usage, const std::string &name)
    {
        NE_ASSERT(m_buffer == nullptr, "Buffer already created");

        SDL_GPUBufferCreateInfo sdlBCI = {
            .size  = static_cast<Uint32>(size),
            .props = 0, // by comment
        };

        switch (usage) {
        case Usage::VertexBuffer:
            sdlBCI.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            break;
        case Usage::IndexBuffer:
            sdlBCI.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            break;
        default:
            NE_CORE_ASSERT(false, "Invalid buffer usage");
            return;
        }

        m_buffer = SDL_CreateGPUBuffer(&m_device, &sdlBCI);
        NE_CORE_ASSERT(m_buffer, "Failed to create buffer: {}", SDL_GetError());
        m_size = size;
        m_name = name;

        SDL_SetGPUBufferName(&m_device, m_buffer, name.c_str());
    }
};

// RAII wrapper for SDL_GPUTransferBuffer with self-contained size tracking
class SDLGPUTransferBuffer
{
    // Disallow copying
    SDLGPUTransferBuffer(const SDLGPUTransferBuffer &)            = delete;
    SDLGPUTransferBuffer &operator=(const SDLGPUTransferBuffer &) = delete;

    SDL_GPUDevice         &m_device; // Reference to ensure device outlives buffer
    SDL_GPUTransferBuffer *m_buffer = nullptr;
    size_t                 m_size   = 0;
    std::string            m_name;

  private:
    // Private constructor to ensure creation through factory method
    SDLGPUTransferBuffer(SDL_GPUDevice &device)
        : m_device(device) {}

  public:
    enum class Usage
    {
        Upload,
        Download,
    };

    ~SDLGPUTransferBuffer()
    {
        if (m_buffer) {
            SDL_ReleaseGPUTransferBuffer(&m_device, m_buffer);
        }
        m_buffer = nullptr;
    }

    SDL_GPUTransferBuffer *getBuffer() const { return m_buffer; }
    size_t                 getSize() const { return m_size; }
    const std::string     &getName() const { return m_name; }

    // Static factory method
    static SDLGPUTransferBufferPtr Create(SDL_GPUDevice &device, const std::string &name, Usage usage, size_t size)
    {
        auto ptr = std::shared_ptr<SDLGPUTransferBuffer>(new SDLGPUTransferBuffer(device));
        ptr->createInternal(size, usage, name);
        return ptr;
    }

    // Method to extend the buffer size if needed
    void tryExtendSize(const std::string &name, Usage usage, size_t requiredSize)
    {
        if (requiredSize <= m_size) {
            return;
        }

        // Calculate new size (double the current size or use required size if larger)
        size_t newSize = m_size * 2;
        if (newSize < requiredSize) {
            newSize = requiredSize;
        }

        SDL_ReleaseGPUTransferBuffer(&m_device, m_buffer);
        m_buffer = nullptr;
        createInternal(newSize, usage, name);
    }

  private:
    void createInternal(std::size_t size, Usage usage, const std::string &name)
    {
        NE_ASSERT(m_buffer == nullptr, "Transfer buffer already created");

        SDL_GPUTransferBufferCreateInfo createInfo = {
            .size  = static_cast<Uint32>(size),
            .props = 0, // by comment
        };

        switch (usage) {
        case Usage::Upload:
            createInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            break;
        case Usage::Download:
            createInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            break;
        default:
            NE_CORE_ASSERT(false, "Invalid transfer buffer usage");
            return;
        }

        m_buffer = SDL_CreateGPUTransferBuffer(&m_device, &createInfo);
        NE_CORE_ASSERT(m_buffer, "Failed to create transfer buffer: {}", SDL_GetError());
        m_size = size;
        m_name = name;

        // Note: No name setting for transfer buffer as it's not supported in the SDK
    }
};