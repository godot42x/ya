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
  public:
    enum class Usage
    {
        VertexBuffer,
        IndexBuffer,
        // Add other usages as needed
    };

  private:

    SDL_GPUDevice &_device; // Reference to ensure device outlives buffer
    SDL_GPUBuffer *_gpuBuffer = nullptr;
    std::size_t    _size;
    std::string    _name;
    Usage          _usage;



  public:
    // Private constructor to ensure creation through factory method
    SDLGPUBuffer(SDL_GPUDevice &device)
        : _device(device) {}

    ~SDLGPUBuffer()
    {
        if (_gpuBuffer) {
            NE_CORE_TRACE("Destroying gpu buffer: {}", _name);
            SDL_ReleaseGPUBuffer(&_device, _gpuBuffer);
        }
        _gpuBuffer = nullptr;
    }

  private:
    // Disallow copying
    SDLGPUBuffer(const SDLGPUBuffer &)            = delete;
    SDLGPUBuffer &operator=(const SDLGPUBuffer &) = delete;

  public:

    SDL_GPUBuffer *getBuffer() const { return _gpuBuffer; }
    std::size_t    getSize() const { return _size; }
    std::string    getName() const { return _name; }
    SDL_GPUDevice *getDevice() const { return &_device; }

    // Static factory method
    static SDLGPUBufferPtr Create(SDL_GPUDevice *device, const std::string &name, Usage usage, size_t size)
    {
        auto ptr = std::make_shared<SDLGPUBuffer>(*device);
        NE_CORE_TRACE("Creating gpu buffer: {}", name);
        ptr->createInternal(size, usage, name);
        return ptr;
    }

    // Method to recreate buffer with larger size if needed
    void tryExtendSize(size_t requiredSize)
    {
        if (requiredSize <= _size) {
            return;
        }

        // Calculate new size (double the current size or use required size if larger)
        if (requiredSize < _size * 2) {
            requiredSize = _size * 2;
        }

        SDL_ReleaseGPUBuffer(&_device, _gpuBuffer);
        NE_CORE_TRACE("Extend set buffer size nullptr: {} -> {}", _size, requiredSize);
        _gpuBuffer = nullptr;
        createInternal(requiredSize, _usage, _name);
    }


  private:
    void createInternal(std::size_t size, Usage usage, const std::string &name)
    {
        NE_CORE_ASSERT(_gpuBuffer == nullptr, "Buffer already created, name: {}", name);

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

        _gpuBuffer = SDL_CreateGPUBuffer(&_device, &sdlBCI);
        NE_CORE_ASSERT(_gpuBuffer, "Failed to create buffer: {}", SDL_GetError());
        _size = size;
        _name = name;

        SDL_SetGPUBufferName(&_device, _gpuBuffer, name.c_str());
    }
};

// RAII wrapper for SDL_GPUTransferBuffer with self-contained size tracking
class SDLGPUTransferBuffer
{
  public:
    enum class Usage
    {
        Upload,
        Download,
    };

  private:

    SDL_GPUDevice         &_device; // Reference to ensure device outlives buffer
    SDL_GPUTransferBuffer *_gpuBuffer = nullptr;
    size_t                 _size      = 0;
    std::string            _name;
    Usage                  _usage;

  private:



    // Disallow copying
    SDLGPUTransferBuffer(const SDLGPUTransferBuffer &)            = delete;
    SDLGPUTransferBuffer &operator=(const SDLGPUTransferBuffer &) = delete;

  public:
    // Private constructor to ensure creation through factory method
    SDLGPUTransferBuffer(SDL_GPUDevice &device)
        : _device(device) {}

    ~SDLGPUTransferBuffer()
    {
        if (_gpuBuffer) {
            NE_CORE_TRACE("Destroying transfer buffer: {}", _name);
            SDL_ReleaseGPUTransferBuffer(&_device, _gpuBuffer);
        }
        _gpuBuffer = nullptr;
    }


  public:

    SDL_GPUTransferBuffer *getBuffer() const { return _gpuBuffer; }
    size_t                 getSize() const { return _size; }
    const std::string     &getName() const { return _name; }

    // Static factory method
    static SDLGPUTransferBufferPtr Create(SDL_GPUDevice *device, const std::string &name, Usage usage, size_t size)
    {
        auto ptr = std::make_shared<SDLGPUTransferBuffer>(*device);
        ptr->createInternal(size, usage, name);
        return ptr;
    }

    // Method to extend the buffer size if needed
    void tryExtendSize(size_t requiredSize)
    {
        if (requiredSize <= _size) {
            return;
        }

        // Calculate new size (double the current size or use required size if larger)
        if (requiredSize < _size * 2) {
            requiredSize = _size * 2;
        }


        SDL_ReleaseGPUTransferBuffer(&_device, _gpuBuffer);
        _gpuBuffer = nullptr;
        createInternal(requiredSize, _usage, _name);
    }

  private:
    void createInternal(std::size_t size, Usage usage, const std::string &name)
    {
        NE_CORE_ASSERT(_gpuBuffer == nullptr, "Transfer buffer already created name: {} ", name);

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

        _gpuBuffer = SDL_CreateGPUTransferBuffer(&_device, &createInfo);
        NE_CORE_ASSERT(_gpuBuffer, "Failed to create transfer buffer: {}", SDL_GetError());
        _size = size;
        _name = name;

        // Note: No name setting for transfer buffer as it's not supported in the SDK
    }
};