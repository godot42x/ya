/**
 * @ Author: godot42
 * @ Create Time: 2024-08-02 10:47:59
 * @ Modified by: @godot42
 * @ Modified time: 2025-03-07 16:34:28
 * @ Description:
 */


#include "utility.cc/file_utils.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

#include "debug.h"


namespace ut
{
namespace file
{



std::optional<std::string> read_all(const std::filesystem::path &filepath)
{
    static constexpr size_t FILE_MAX_SIZE = 1024 * 1024 * 128; // 128 MB

    // Open the file
    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        log(), " Failed to open file: ", filepath;
        return std::nullopt;
    }

    // Get the size of the file
    std::streamsize file_size = f.tellg();

    // Check for empty file
    if (file_size <= 0) {
        log(), "File is empty: ", filepath;
        return std::string(); // Return empty string rather than nullopt for empty files
    }

    // Check if file is too large
    if (file_size > FILE_MAX_SIZE) {
        log(), "exceed the max size of", FILE_MAX_SIZE, ", File is too large:", filepath;
        return std::nullopt;
    }

    // Reset position to beginning of file
    f.seekg(0, std::ios::beg);

    // Allocate memory to store the file content
    std::string buffer;
    buffer.resize(static_cast<size_t>(file_size));

    // Read the file content into the allocated memory
    if (!f.read(buffer.data(), file_size)) {
        log(), "Failed to read file: ", filepath; // Use consistent logging
        return std::nullopt;
    }
    // No need to explicitly close the file, RAII will handle it

    // Return the buffer directly, no std::move needed (copy elision will optimize this)
    return buffer;
}



ImageInfo ImageInfo::detect(const std::filesystem::path &filepath)
{

    // Define image formats with their extensions and signatures
    struct FormatData
    {
        std::string                extension;
        std::vector<unsigned char> signature;
        size_t                     sig_length;
        ImageInfo::Format          format;
        std::string                format_name;
    };

    const std::vector<FormatData> formats = {
        {.extension = ".png", .signature = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, .sig_length = 8, .format = ImageInfo::Format::PNG, .format_name = "PNG"},
        {.extension = ".jpg", .signature = {0xFF, 0xD8, 0xFF}, .sig_length = 3, .format = ImageInfo::Format::JPEG, .format_name = "JPEG"},
        {.extension = ".jpeg", .signature = {0xFF, 0xD8, 0xFF}, .sig_length = 3, .format = ImageInfo::Format::JPEG, .format_name = "JPEG"},
        {.extension = ".bmp", .signature = {'B', 'M'}, .sig_length = 2, .format = ImageInfo::Format::BMP, .format_name = "BMP"},
        {.extension = ".gif", .signature = {'G', 'I', 'F', '8'}, .sig_length = 4, .format = ImageInfo::Format::GIF, .format_name = "GIF"},
        {.extension = ".webp", .signature = {'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x00, 'W', 'E', 'B', 'P'}, .sig_length = 12, .format = ImageInfo::Format::WEBP, .format_name = "WebP"},
        {.extension = ".tif", .signature = {0x49, 0x49, 0x2A, 0x00}, .sig_length = 4, .format = ImageInfo::Format::TIFF, .format_name = "TIFF"},
        {.extension = ".tiff", .signature = {0x4D, 0x4D, 0x00, 0x2A}, .sig_length = 4, .format = ImageInfo::Format::TIFF, .format_name = "TIFF"}};

    // Initialize result
    ImageInfo info;
    info.file_path = filepath;

    // Open the file to check signature
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        log(), "Failed to open file: ", filepath;
        return info;
    }

    // Read enough bytes for the signature check (use largest signature size)
    std::vector<unsigned char> header(12);
    if (!file.read(reinterpret_cast<char *>(header.data()), header.size())) {
        log(), "Failed to read file header: ", filepath;
        return info;
    }

    // Get lowercase extension
    auto ext = filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    // Check formats by signature first
    for (const auto &fmt : formats) {
        if (fmt.sig_length <= header.size() &&
            std::equal(fmt.signature.begin(), fmt.signature.end(), header.begin())) {
            info.format      = fmt.format;
            info.format_name = fmt.format_name;
            info.bValid      = true;
            return info;
        }
    }

    // If signature check failed, try extension as fallback
    for (const auto &fmt : formats) {
        if (ext == fmt.extension) {
            info.format      = fmt.format;
            info.format_name = fmt.format_name;
            // Mark as not fully validated since we only matched by extension
            info.bValid = false;
            log(), "Warning: File extension suggests ", fmt.format_name,
                " but signature doesn't match: ", filepath;
            return info;
        }
    }

    log(), "Unknown image format: ", filepath;
    return info;
}

std::optional<size_t> get_content_hash(const std::filesystem::path &filepath)
{
    if (auto content = ut::file::read_all(filepath)) {
        return ut::file::get_hash(*content);
    }
    return {};
}

std::optional<size_t> get_hash(const std::string &text)
{
#if 1
    std::hash<std::string> hasher;
    auto                   value = hasher(text);
    return value;

#else
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX    sha256;
    SHA256_Init(&sha256);

    std::ifstream shaderFile(shaderFilePath, std::ios::binary);
    if (!shaderFile.is_open()) {
        // Handle error
    }

    char buffer[4096];
    while (shaderFile.read(buffer, 4096)) {
        SHA256_Update(&sha256, buffer, shaderFile.gcount());
    }
    SHA256_Update(&sha256, buffer, shaderFile.gcount());
    SHA256_Final(hash, &sha256);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        shaderHash += hex;
    }
#endif

    return {};
}


} // namespace file

} // namespace ut
