#!/usr/bin/env python3
"""Generator coverage for [YaVertexInput].

A marked vertex-entry struct is emitted with SPIR-V locations and the natural
C++ layout. An unmarked vertex struct is not. A uniform struct still is.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

SHADER_DIR = Path(__file__).resolve().parent
REPO = SHADER_DIR.parents[1]
sys.path.insert(0, str(SHADER_DIR))

import slang_gen_header as gen  # noqa: E402

SLANG_ROOT = REPO / "Engine" / "Shader" / "Slang"

MARKED = """
#include "Common/VertexInput.slang"

struct FrameData
{
    float4x4 viewProj;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData, Std140DataLayout> uFrame;

struct MeshVertex
{
    [[vk::location(0)]] float3 pos : POSITION;
};

[YaVertexInput]
struct SpriteInstance
{
    float3 worldCenter;
    uint   textureIndex;
    float3 axisX;
    float3 axisY;
    float4 uvRect;
    float4 tint;
};

struct VSOut
{
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VSOut vertWorldMain(MeshVertex input, [[vk::location(3)]] SpriteInstance instance)
{
    VSOut output;
    float3 worldPos = instance.worldCenter + instance.axisX + instance.axisY + input.pos;
    output.sv_position = mul(uFrame.viewProj, float4(worldPos, 1.0));
    output.sv_position += float4(instance.uvRect.xyz + instance.tint.xyz, float(instance.textureIndex));
    return output;
}
"""

UNMARKED = """
struct FrameData
{
    float4 value;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameData> uFrame;

struct MeshVertex
{
    [[vk::location(0)]] float3 pos : POSITION;
};

struct VSOut
{
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VSOut vertMain(MeshVertex input)
{
    VSOut output;
    output.sv_position = float4(input.pos, 1.0) + uFrame.value;
    return output;
}
"""


def _generate(source: str, name: str, output_dir: Path) -> str:
    slang_file = output_dir / f"{name}.slang"
    slang_file.write_text(source, encoding="utf-8")
    gen._process_one_slang(
        slang_file,
        output_dir,
        "ya::slang_types",
        "vertMain",
        True,
        [SLANG_ROOT, output_dir],
        output_dir,
    )
    header = output_dir / f"{name}.slang.h"
    if not header.exists():
        raise SystemExit(f"no header generated for {name}")
    return header.read_text(encoding="utf-8")


def _spirv_locations(spv: Path) -> dict[str, str]:
    dis = Path(gen.find_slangc()).with_name("spirv-dis")
    result = subprocess.run([str(dis), str(spv)], capture_output=True, text=True, check=True)
    names: dict[str, str] = {}
    id_to_name: dict[str, str] = {}
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("OpName"):
            parts = stripped.split()
            if len(parts) >= 3:
                id_to_name[parts[1]] = parts[2].strip('"')
        elif "Location" in stripped and stripped.startswith("OpDecorate"):
            parts = stripped.split()
            # OpDecorate %id Location N
            if len(parts) >= 4 and parts[2] == "Location":
                names[id_to_name.get(parts[1], parts[1])] = parts[3]
    return names


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="ya-vertex-input-") as tmp:
        output_dir = Path(tmp)
        marked = _generate(MARKED, "marked", output_dir)
        if "struct MeshVertex" in marked:
            raise SystemExit("unmarked vertex struct MeshVertex was emitted")
        if "struct YaVertexInputAttribute" in marked:
            raise SystemExit("attribute type was emitted")
        if "struct alignas(16) FrameData" not in marked:
            raise SystemExit(f"uniform FrameData missing:\n{marked}")
        for needle in (
            "struct SpriteInstance",
            "static_assert(sizeof(SpriteInstance) == 72,",
            'static_assert(SLANG_OFFSETOF(SpriteInstance, worldCenter) == 0,',
            'static_assert(SLANG_OFFSETOF(SpriteInstance, textureIndex) == 12,',
            'static_assert(SLANG_OFFSETOF(SpriteInstance, axisX) == 16,',
            'static_assert(SLANG_OFFSETOF(SpriteInstance, axisY) == 28,',
            'static_assert(SLANG_OFFSETOF(SpriteInstance, uvRect) == 40,',
            'static_assert(SLANG_OFFSETOF(SpriteInstance, tint) == 56,',
            '{"worldCenter", 3, 0, 3, VertexInputScalar::Float32}',
            '{"textureIndex", 4, 12, 1, VertexInputScalar::Uint32}',
            '{"axisX", 5, 16, 3, VertexInputScalar::Float32}',
            '{"axisY", 6, 28, 3, VertexInputScalar::Float32}',
            '{"uvRect", 7, 40, 4, VertexInputScalar::Float32}',
            '{"tint", 8, 56, 4, VertexInputScalar::Float32}',
        ):
            if needle not in marked:
                raise SystemExit(f"missing {needle!r} in:\n{marked}")

        locations = _spirv_locations(output_dir / "marked.spv")
        expected = {
            "instance.worldCenter": "3",
            "instance.textureIndex": "4",
            "instance.axisX": "5",
            "instance.axisY": "6",
            "instance.uvRect": "7",
            "instance.tint": "8",
            "input.pos": "0",
        }
        for name, location in expected.items():
            if locations.get(name) != location:
                raise SystemExit(f"SPIR-V location for {name} is {locations.get(name)}, expected {location}\n{locations}")

        broken = {
            "name": "Broken",
            "fields": [{
                "name": "pos",
                "type": {
                    "kind": "vector",
                    "elementCount": 3,
                    "elementType": {"kind": "scalar", "scalarType": "float32"},
                },
                "binding": {"kind": "varyingInput"},
            }],
        }
        try:
            gen.gen_vertex_input_struct(broken, 0)
        except ValueError as error:
            if "pos" not in str(error) or "location" not in str(error):
                raise SystemExit(f"missing-location error was {error}")
        else:
            raise SystemExit("a field without a location was emitted")

        unmarked = _generate(UNMARKED, "unmarked", output_dir)
        if "struct MeshVertex" in unmarked or "VertexInputField" in unmarked:
            raise SystemExit(f"unmarked shader grew a vertex record:\n{unmarked}")
        if "struct alignas(16) FrameData" not in unmarked:
            raise SystemExit("unmarked shader lost its uniform struct")

    print("vertex input header generation: ok")


if __name__ == "__main__":
    main()
