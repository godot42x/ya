#pragma once

#include "Core/Base.h"
#include "Core/Profiling/BenchmarkConfig.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
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
    profiling::FBenchmarkCommonConfig common;
    std::optional<std::string>        scene;
    bool                              bUncapFrameRate = true;
    /// Simulation step per frame, independent of wall time, so animated
    /// content advances identically between runs. 0 = real frame time.
    float                fixedDeltaSeconds = 1.0f / 60.0f;
    bool                 bCpuTrace         = false;
    FBenchmarkCameraPath cameraPath;
};

/// Strict: unknown keys and malformed values are errors, so a typo in a
/// config cannot silently produce a different benchmark.
[[nodiscard]] YA_GAME_RUNTIME_API bool parseBenchmarkConfig(const nlohmann::json& json, BenchmarkConfig& out, std::string& error);
[[nodiscard]] YA_GAME_RUNTIME_API bool loadBenchmarkConfig(const std::string& path, BenchmarkConfig& out, std::string& error);

} // namespace ya
