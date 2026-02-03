import os
import stat


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


root_dir = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))

READ_ONLY = 0o555


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


third_party_dirs = [
    "Engine/ThirdParty/ImGui",
    "Engine/ThirdParty/ImGuizmo",
    "Engine/ThirdParty/LearnOpenGL",
    "Engine/ThirdParty/Vulkan-Samples-Assets",
]

import time
now = time.time()
for dir_name in third_party_dirs:
    set_readonly_recursive(os.path.join(root_dir, dir_name))
print(f"-- Setup 3rd party read-only took {time.time() - now:.2f} seconds")
