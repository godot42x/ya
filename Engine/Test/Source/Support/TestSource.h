#pragma once

// ============================================================================
// TestSource - locating engine sources from a running test.
//
// Some guards assert on the *text* of a source file ("no platform non-client
// API leaks into GUI/editor code", "this header documents the rule it
// enforces", ...). They need the repo root, and the obvious `__FILE__` plus a
// fixed "../.." arithmetic rots silently the moment the test file moves into
// another suite directory -- which is exactly what the per-gate Source layout
// does to files. Walk up to a marker instead, so a source-reading guard can be
// filed anywhere under Source/.
// ============================================================================

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

namespace ya::test
{

/// Repo root, found by walking up from this header until the marker pair that
/// only the root carries (`Engine/Source` + `xmake`) shows up. Two candidate
/// starting points: where this header was compiled from, and the process
/// working directory (the build pins it to the project dir via set_rundir).
[[nodiscard]] inline const std::filesystem::path& repoRoot()
{
    static const std::filesystem::path root = []() -> std::filesystem::path {
        const auto hasMarker = [](const std::filesystem::path& dir) {
            std::error_code existsError;
            return std::filesystem::exists(dir / "Engine" / "Source", existsError) &&
                   std::filesystem::exists(dir / "xmake", existsError);
        };

        std::error_code error;
        const auto      compiledFrom = std::filesystem::absolute(__FILE__, error);
        const auto      workingDir   = std::filesystem::current_path(error);

        for (const std::filesystem::path& start : {compiledFrom, workingDir}) {
            if (start.empty()) {
                continue;
            }
            for (std::filesystem::path probe = start.parent_path(); !probe.empty(); probe = probe.parent_path()) {
                if (hasMarker(probe)) {
                    return probe;
                }
                if (!probe.has_parent_path() || probe.parent_path() == probe) {
                    break;
                }
            }
        }
        return {};
    }();
    return root;
}

/// Read a source file addressed relative to `<repo>/Engine`, for example
/// "Source/Framework/GUI/Host/Window/GUIWindowChrome.cpp".
[[nodiscard]] inline std::string readEngineSource(const std::filesystem::path& relative)
{
    const std::filesystem::path path = repoRoot() / "Engine" / relative;
    std::ifstream               in(path);
    EXPECT_TRUE(in.good()) << "missing " << path.string()
                           << " (repo root: " << (repoRoot().empty() ? "<not found>" : repoRoot().string()) << ")";
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace ya::test
