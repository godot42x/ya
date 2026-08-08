#pragma once

#include "Core/Base.h"

namespace ya
{


struct FAssetFile
{
    std::string _filepath;
    std::string _filename;
    std::string _extension;

    static constexpr uint32_t MAGIC_NUMBER = 0xDEADBEEF; // Optional: for simple file validation
    std::string               fileType;

    std::string _data; // Optional: raw file data for in-memory assets

    FAssetFile() = default;

    explicit FAssetFile(std::string_view filepath)
        : _filepath(filepath)
    {
        parseFilepath();
    }

  private:
    void parseFilepath()
    {
        size_t lastSlash = _filepath.find_last_of("/\\");
        size_t lastDot   = _filepath.find_last_of('.');

        if (lastSlash != std::string::npos) {
            _filename = _filepath.substr(lastSlash + 1, lastDot - lastSlash - 1);
        }
        else {
            _filename = _filepath.substr(0, lastDot);
        }

        if (lastDot != std::string::npos) {
            _extension = _filepath.substr(lastDot + 1);
        }
        else {
            _extension.clear();
        }
    }

    bool writeToFile() const
    {
        std::ofstream file(_filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        file.write(reinterpret_cast<const char *>(&MAGIC_NUMBER), sizeof(MAGIC_NUMBER));
        auto typeHash = std::hash<std::string>{}(fileType);

        // file.write(std::to_string(typeHash).c_str(), sizeof(typeHash));
        // file.write(std::to_string(_data.size()).c_str(), sizeof(uint32_t));
        // file << _data;
        // file.write("END", sizeof("END")); // Optional: end marker

        file.close();
        return true;
    }
};


struct FRenderTexture : public FAssetFile
{
    FRenderTexture() = default;

    // Texture * _runtimeTexture = nullptr; // Optional: pointer to the actual GPU resource

    explicit FRenderTexture(std::string_view filepath)
        : FAssetFile(filepath)
    {
        fileType = "RenderTexture";
    }
};

} // namespace ya