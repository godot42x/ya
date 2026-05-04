import os
import subprocess
import sys
import time


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
root_dir = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

READ_ONLY = 0o555


def set_readonly_recursive(path):
    """Recursively set directory and files to read-only."""
    for root, dirs, files in os.walk(path):
        # Set directory permissions
        os.chmod(root, READ_ONLY)
        # Set file permissions
        for file in files:
            os.chmod(os.path.join(root, file), READ_ONLY)


def disable_submodule_filemode(submodule_path):
    """Set core.fileMode=false in a submodule's local config.

    The chmod pass above flips the executable bit on macOS, which git records
    as a mode diff (Windows git doesn't track mode bits, so Windows users never
    see the churn). Disabling fileMode locally in each submodule makes
    `git status` ignore mode-only diffs so VS Code's SCM view stays clean.
    This is a local-only config, never committed upstream.
    """
    if not os.path.isdir(submodule_path):
        return
    # Submodules have a .git FILE (gitlink) pointing to a gitdir under the
    # parent's .git/modules. `git -C <path> config` handles that indirection.
    try:
        subprocess.run(
            ["git", "-C", submodule_path, "config", "--local", "core.fileMode", "false"],
            check=True,
            capture_output=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"-- warn: failed to set core.fileMode=false for {submodule_path}: {e}", file=sys.stderr)


# Submodules touched by the chmod pass (and the log.cc plugin, which also sees
# phantom mode diffs after its own post-checkout hooks on macOS).
third_party_dirs = [
    "Engine/ThirdParty/ImGui",
    "Engine/ThirdParty/ImGuizmo",
    "Engine/ThirdParty/LearnOpenGL",
    "Engine/ThirdParty/Vulkan-Samples-Assets",
]

submodules_needing_filemode_off = third_party_dirs + [
    "Engine/Plugins/log.cc",
    "Engine/Plugins/utility.cc",
]


now = time.time()
for dir_name in third_party_dirs:
    set_readonly_recursive(os.path.join(root_dir, dir_name))
print(f"-- Setup 3rd party read-only took {time.time() - now:.2f} seconds")

# Suppress phantom mode-bit diffs in submodules on macOS/Linux. No-op on
# Windows (git already ignores mode bits there) but harmless to apply anyway.
for sm in submodules_needing_filemode_off:
    disable_submodule_filemode(os.path.join(root_dir, sm))
print("-- Disabled core.fileMode on submodules to avoid phantom diffs")
