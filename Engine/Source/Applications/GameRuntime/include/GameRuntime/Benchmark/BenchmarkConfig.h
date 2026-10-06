#pragma once

#include "Core/Base.h"
#include "RHI/RenderDefines.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ya
{

/// Camera pose at one benchmark frame. Rotation is the transform's euler
/// degrees (pitch, yaw, roll), interpolated per component.
struct FBenchmarkCameraKeyframe
{
    uint64_t  frame = 0;
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
};

/// Keyframes sorted by strictly increasing frame. Frame 0 is the first frame
/// of the run (warmup included), so warmup and sampling replay the same path.
struct FBenchmarkCameraPath
{
    std::vector<FBenchmarkCameraKeyframe> keyframes;
    /// After the last keyframe: hold it. Looping maps `frame` with
    /// `phase = (frame - first) % (last - first)`, so the last frame
    /// coincides with the first (the last keyframe is the seam).
    bool bLoop = false;
};

struct FBenchmarkCameraPose
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
};

/// Pose at `frame`: linear between the two keyframes around it, the first
/// keyframe before it, the last after it. Looping uses
/// `phase = (frame - first) % (last - first)` once `frame` reaches the first
/// keyframe, so the last frame coincides with the first. Nullopt for an empty
/// path, which leaves the scene camera alone.
[[nodiscard]] YA_GAME_RUNTIME_API std::optional<FBenchmarkCameraPose> sampleBenchmarkCameraPath(const FBenchmarkCameraPath& path,
                                                                                                uint64_t                    frame);

struct BenchmarkConfig
{
    std::string                sourcePath;
    std::string                name;
    std::optional<std::string> scene;
    uint32_t                   width  = 1920;
    uint32_t                   height = 1080;
    /// Requested; the run falls back to another non-vsync mode the surface
    /// offers, FIFO only as the last resort, and records what it got.
    EPresentMode::T presentMode       = EPresentMode::Immediate;
    bool            bUncapFrameRate   = true;
    uint64_t        warmupFrames      = 120;
    uint64_t        sampleFrames      = 600;
    /// Simulation step per frame, independent of wall time, so animated
    /// content advances identically between runs. 0 = real frame time.
    float           fixedDeltaSeconds = 1.0f / 60.0f;
    bool            bCpuTrace         = false;
    std::string     outputDir         = "Engine/Saved/Benchmark";
    FBenchmarkCameraPath cameraPath;
};

[[nodiscard]] YA_GAME_RUNTIME_API const char* toBenchmarkPresentModeLabel(EPresentMode::T mode);
[[nodiscard]] YA_GAME_RUNTIME_API bool        tryParseBenchmarkPresentMode(std::string_view text, EPresentMode::T& outMode);

/// Pick from what the surface offers: the requested mode, then the other
/// non-vsync modes (Immediate, Mailbox, FIFO relaxed), FIFO last.
[[nodiscard]] YA_GAME_RUNTIME_API EPresentMode::T chooseBenchmarkPresentMode(EPresentMode::T                     requested,
                                                                             const std::vector<EPresentMode::T>& available);

/// Strict: unknown keys and malformed values are errors, so a typo in a
/// config cannot silently produce a different benchmark.
[[nodiscard]] YA_GAME_RUNTIME_API bool parseBenchmarkConfig(const nlohmann::json& json, BenchmarkConfig& out, std::string& error);
[[nodiscard]] YA_GAME_RUNTIME_API bool loadBenchmarkConfig(const std::string& path, BenchmarkConfig& out, std::string& error);

} // namespace ya
