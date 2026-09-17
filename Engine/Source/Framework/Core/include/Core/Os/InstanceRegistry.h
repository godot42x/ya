#pragma once

#include "Core/Api.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ya::Os
{

/// What a live engine instance is, and where to reach it.
///
/// This is the discovery half of the one-instance rule. ProcessLock answers "may
/// I start"; the record answers "who is already running, and how do I talk to
/// them". Tooling reads the record instead of guessing: attaching to the live
/// editor becomes a lookup, not another process. Without it, every tool that
/// wants to drive the engine has to launch its own, because there is nothing to
/// find and nothing that says a run is already there.
struct FInstanceRecord
{
    /// The claim key this record belongs to. Identifies the owner, so a reader
    /// never has to reproduce the file-name hashing to know what it read.
    std::string key;
    /// Project the run belongs to; empty for project-less runs.
    std::string project;
    /// Product mode, e.g. "editor" / "game".
    std::string mode;
    /// Owning process. writeInstanceRecord fills this in when left 0.
    uint32_t pid = 0;
    /// Automation control port; 0 when the run serves no automation requests.
    uint16_t controlPort = 0;
    int64_t startedAtUnixMs = 0;
};

/// Per-user directory holding the claims (`.lock`) and records
/// (`.instance.json`) of live instances.
[[nodiscard]] YA_CORE_API std::filesystem::path instanceDirectory();

/// `<sanitized>-<hash>`: the file stem a claim and its record share. The hash
/// keeps keys that sanitize to the same prefix apart, and bounds the name.
[[nodiscard]] YA_CORE_API std::string instanceFileStem(std::string_view key);

[[nodiscard]] YA_CORE_API std::filesystem::path resolveInstanceRecordPath(std::string_view key);

/// Publish p record. Written as an ordinary file, not exclusively held: another
/// process has to read it while this one still runs, which the claim file
/// cannot offer. p record's pid is filled in when it is 0.
[[nodiscard]] YA_CORE_API bool writeInstanceRecord(FInstanceRecord record);

/// The record for p key, when one is published. Lifetime is the caller's to
/// check: a record outlives a process that was killed. See ProcessLock, which
/// is what answers whether the instance is actually still there.
[[nodiscard]] YA_CORE_API std::optional<FInstanceRecord> readInstanceRecord(std::string_view key);

[[nodiscard]] YA_CORE_API bool removeInstanceRecord(std::string_view key);

} // namespace ya::Os
