#include "Core/Os/InstanceRegistry.h"

#include "Core/Log.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

#if defined(_WIN32)
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace ya::Os
{

namespace
{

/// The record is read by tooling (the control client), not by the engine, so it
/// is a flat `key=value` line list: it can be written and read without pulling a
/// serialization dependency into Core, and a reader that does not know a field
/// simply ignores it.
constexpr std::string_view kRecordSuffix = ".instance.json";

// Named apart from OsProcessLock.cpp's helper on purpose: the two files end up
// in one unity translation unit, where same-named internal functions collide.
uint32_t thisProcessId()
{
#if defined(_WIN32)
    return static_cast<uint32_t>(::_getpid());
#else
    return static_cast<uint32_t>(::getpid());
#endif
}

int64_t nowUnixMillis()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

void appendField(std::string& out, std::string_view name, const std::string& value)
{
    out.append(name);
    out.push_back('=');
    out.append(value);
    out.push_back('\n');
}

void appendField(std::string& out, std::string_view name, int64_t value)
{
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%lld", static_cast<long long>(value));
    appendField(out, name, std::string(buffer.data()));
}

std::string serializeRecord(const FInstanceRecord& record)
{
    std::string out;
    out.reserve(256);
    appendField(out, "pid", static_cast<int64_t>(record.pid));
    appendField(out, "mode", record.mode);
    appendField(out, "project", record.project);
    appendField(out, "controlPort", static_cast<int64_t>(record.controlPort));
    appendField(out, "startedAtUnixMs", record.startedAtUnixMs);
    appendField(out, "key", record.key);
    return out;
}

FInstanceRecord parseRecord(const std::string& text)
{
    FInstanceRecord record;
    std::istringstream stream(text);
    std::string        line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        // Split on the first '=' only: a project path may contain '='.
        const std::string name  = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (name == "pid") {
            record.pid = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (name == "mode") {
            record.mode = value;
        }
        else if (name == "project") {
            record.project = value;
        }
        else if (name == "controlPort") {
            record.controlPort = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (name == "startedAtUnixMs") {
            record.startedAtUnixMs = std::strtoll(value.c_str(), nullptr, 10);
        }
        else if (name == "key") {
            record.key = value;
        }
    }
    return record;
}

} // namespace

std::filesystem::path instanceDirectory()
{
    std::error_code ec;
    auto            dir = std::filesystem::temp_directory_path(ec);
    if (ec || dir.empty()) {
        dir = std::filesystem::current_path(ec);
    }
    return dir / "ya-instances";
}

std::string instanceFileStem(std::string_view key)
{
    std::string stem;
    stem.reserve(key.size() + 16);
    for (const char c : key) {
        const bool bKeep = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.';
        stem.push_back(bKeep ? c : '_');
    }
    if (stem.size() > 96) {
        stem.resize(96);
    }
    stem.push_back('-');
    stem.append(std::to_string(std::hash<std::string>{}(std::string(key))));
    return stem;
}

std::filesystem::path resolveInstanceRecordPath(std::string_view key)
{
    return instanceDirectory() / (instanceFileStem(key) + std::string(kRecordSuffix));
}

bool writeInstanceRecord(FInstanceRecord record)
{
    if (record.key.empty()) {
        return false;
    }
    if (record.pid == 0) {
        record.pid = thisProcessId();
    }
    if (record.startedAtUnixMs == 0) {
        record.startedAtUnixMs = nowUnixMillis();
    }

    const std::filesystem::path path = resolveInstanceRecordPath(record.key);
    std::error_code             ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    // Write beside the target and rename, so a reader either sees the previous
    // record or the complete new one -- never a half-written line list.
    const std::filesystem::path temporary =
        path.parent_path() / (path.filename().string() + ".tmp" + std::to_string(record.pid));
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            YA_CORE_WARN("InstanceRegistry: cannot write '{}'", temporary.string());
            return false;
        }
        file << serializeRecord(record);
    }

    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        YA_CORE_WARN("InstanceRegistry: cannot publish '{}': {}", path.string(), ec.message());
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

std::optional<FInstanceRecord> readInstanceRecord(std::string_view key)
{
    if (key.empty()) {
        return std::nullopt;
    }
    std::ifstream file(resolveInstanceRecordPath(key), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseRecord(buffer.str());
}

bool removeInstanceRecord(std::string_view key)
{
    if (key.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::remove(resolveInstanceRecordPath(key), ec);
}

} // namespace ya::Os
