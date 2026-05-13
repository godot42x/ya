#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


DEFAULT_RENDERDOCCMD_WINDOWS = Path("C:/Program Files/RenderDoc/renderdoccmd.exe")
DRAW_COMMANDS = {
    "vkCmdDraw",
    "vkCmdDrawIndexed",
    "vkCmdDrawIndirect",
    "vkCmdDrawIndexedIndirect",
    "vkCmdDrawIndirectCount",
    "vkCmdDrawIndexedIndirectCount",
    "vkCmdDrawMeshTasksEXT",
    "vkCmdDrawMeshTasksIndirectEXT",
    "vkCmdDrawMeshTasksIndirectCountEXT",
}
DRAW_INDEXED_COMMANDS = {
    "vkCmdDrawIndexed",
    "vkCmdDrawIndexedIndirect",
    "vkCmdDrawIndexedIndirectCount",
}
DISPATCH_COMMANDS = {
    "vkCmdDispatch",
    "vkCmdDispatchBase",
    "vkCmdDispatchIndirect",
    "vkCmdTraceRaysKHR",
    "vkCmdTraceRaysIndirectKHR",
    "vkCmdTraceRaysIndirect2KHR",
}
BEGIN_LABEL_COMMANDS = {
    "vkCmdBeginDebugUtilsLabelEXT",
    "vkCmdBeginDebugUtilsLabel",
    "vkCmdBeginDebugMarkerEXT",
}
END_LABEL_COMMANDS = {
    "vkCmdEndDebugUtilsLabelEXT",
    "vkCmdEndDebugUtilsLabel",
    "vkCmdEndDebugMarkerEXT",
}
BEGIN_PASS_COMMANDS = {
    "vkCmdBeginRendering",
    "vkCmdBeginRenderPass",
    "vkCmdBeginRenderPass2",
}
END_PASS_COMMANDS = {
    "vkCmdEndRendering",
    "vkCmdEndRenderPass",
    "vkCmdEndRenderPass2",
}


class PassTreeBuilder:
    def __init__(self) -> None:
        self.root = self._make_node(label="Capture", kind="root", event_start=None, trace_start=None)
        self._stack: list[dict[str, Any]] = [self.root]
        self._next_node_id = 1
        self._last_chunk_index: int | None = None
        self._last_trace_end: int | None = None

    def _make_node(self, label: str, kind: str, event_start: int | None, trace_start: int | None) -> dict[str, Any]:
        node = {
            "id": self._next_node_id if kind != "root" else 0,
            "kind": kind,
            "label": label,
            "name": label,
            "children": [],
            "drawCount": 0,
            "drawIndexedCount": 0,
            "dispatchCount": 0,
            "eventStart": event_start,
            "eventEnd": None,
            "traceStart": trace_start,
            "traceDuration": None,
            "_traceEnd": None,
        }
        if kind != "root":
            self._next_node_id += 1
        return node

    def _normalize_label(self, label: str | None, fallback: str) -> str:
        if label is None:
            return fallback
        text = label.strip()
        return text or fallback

    def begin_node(self, label: str | None, kind: str, chunk_index: int | None, timestamp: int | None) -> None:
        node = self._make_node(
            label=self._normalize_label(label, kind),
            kind=kind,
            event_start=chunk_index,
            trace_start=timestamp,
        )
        self._stack[-1]["children"].append(node)
        self._stack.append(node)
        self._last_chunk_index = chunk_index
        self._last_trace_end = timestamp

    def close_to_kind(self, kind: str, chunk_index: int | None, trace_end: int | None) -> None:
        for index in range(len(self._stack) - 1, 0, -1):
            node = self._stack[index]
            if node["kind"] != kind:
                continue
            while len(self._stack) - 1 >= index:
                current = self._stack.pop()
                self._finalize_node(current, chunk_index, trace_end)
            self._last_chunk_index = chunk_index
            self._last_trace_end = trace_end
            return

    def count_command(self, command_name: str, chunk_index: int | None, timestamp: int | None, duration: int | None) -> None:
        trace_end = None if timestamp is None else timestamp + (duration or 0)
        self._last_chunk_index = chunk_index
        self._last_trace_end = trace_end
        for node in self._stack:
            if command_name in DRAW_COMMANDS:
                node["drawCount"] += 1
            if command_name in DRAW_INDEXED_COMMANDS:
                node["drawIndexedCount"] += 1
            if command_name in DISPATCH_COMMANDS:
                node["dispatchCount"] += 1
            if node["eventStart"] is None:
                node["eventStart"] = chunk_index
            node["eventEnd"] = chunk_index
            if node["traceStart"] is None:
                node["traceStart"] = timestamp
            if trace_end is not None:
                node["_traceEnd"] = trace_end

    def finish(self) -> dict[str, Any]:
        while len(self._stack) > 1:
            current = self._stack.pop()
            self._finalize_node(current, self._last_chunk_index, self._last_trace_end)
        self._finalize_node(self.root, self._last_chunk_index, self._last_trace_end)
        self._strip_internal(self.root)
        return self.root

    def _finalize_node(self, node: dict[str, Any], chunk_index: int | None, trace_end: int | None) -> None:
        if node["eventEnd"] is None:
            node["eventEnd"] = chunk_index
        if trace_end is not None:
            node["_traceEnd"] = trace_end
        start = node["traceStart"]
        end = node["_traceEnd"]
        if start is not None and end is not None:
            node["traceDuration"] = max(0, end - start)

    def _strip_internal(self, node: dict[str, Any]) -> None:
        node.pop("_traceEnd", None)
        if node["eventStart"] is None:
            node["eventStart"] = node["eventEnd"]
        for child in node["children"]:
            self._strip_internal(child)


def resolve_renderdoccmd(explicit_path: str | None) -> Path:
    candidates: list[Path] = []
    if explicit_path:
        candidates.append(Path(explicit_path))
    which_result = shutil.which("renderdoccmd")
    if which_result:
        candidates.append(Path(which_result))
    candidates.append(DEFAULT_RENDERDOCCMD_WINDOWS)

    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("找不到 renderdoccmd，请通过 --renderdoccmd 指定路径")


def run_command(command: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", check=check)


def detect_renderdoc_version(renderdoccmd: Path) -> dict[str, Any]:
    try:
        completed = run_command([str(renderdoccmd), "version"])
    except subprocess.CalledProcessError as exc:
        return {
            "path": str(renderdoccmd).replace("\\", "/"),
            "version": None,
            "raw": exc.stderr.strip() or exc.stdout.strip() or None,
        }

    raw = completed.stdout.strip() or completed.stderr.strip()
    return {
        "path": str(renderdoccmd).replace("\\", "/"),
        "version": raw or None,
        "raw": raw or None,
    }


def convert_rdc_to_xml(renderdoccmd: Path, input_path: Path, xml_path: Path) -> None:
    command = [
        str(renderdoccmd),
        "convert",
        "-f",
        str(input_path),
        "-o",
        str(xml_path),
        "-c",
        "xml",
    ]
    completed = run_command(command, check=False)
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip() or "未知错误"
        raise RuntimeError(f"renderdoccmd convert 失败: {message}")


def extract_label(chunk: ET.Element) -> str | None:
    for string_node in chunk.iter("string"):
        if string_node.attrib.get("name") == "pLabelName":
            return string_node.text
    return None


def parse_int(value: str | None) -> int | None:
    if value is None or value == "":
        return None
    return int(value)


def parse_xml_to_summary(xml_path: Path) -> dict[str, Any]:
    builder = PassTreeBuilder()
    header: dict[str, Any] = {}

    context = ET.iterparse(xml_path, events=("end",))
    for _, elem in context:
        if elem.tag == "driver" and elem.text and "driver" not in header:
            header["driver"] = elem.text.strip()
        elif elem.tag == "timebase" and "timebase" not in header:
            header["timebase"] = {
                "base": parse_int(elem.attrib.get("base")),
                "frequency": parse_int(elem.attrib.get("frequency")),
            }
        elif elem.tag == "chunk":
            command_name = elem.attrib.get("name", "")
            chunk_index = parse_int(elem.attrib.get("chunkIndex"))
            timestamp = parse_int(elem.attrib.get("timestamp"))
            duration = parse_int(elem.attrib.get("duration"))
            trace_end = None if timestamp is None else timestamp + (duration or 0)

            if command_name in BEGIN_LABEL_COMMANDS:
                builder.begin_node(extract_label(elem), "debugLabel", chunk_index, timestamp)
            elif command_name in END_LABEL_COMMANDS:
                builder.close_to_kind("debugLabel", chunk_index, trace_end)
            elif command_name in BEGIN_PASS_COMMANDS:
                builder.begin_node(command_name, "renderPass", chunk_index, timestamp)
            elif command_name in END_PASS_COMMANDS:
                builder.close_to_kind("renderPass", chunk_index, trace_end)
            elif command_name in DRAW_COMMANDS or command_name in DISPATCH_COMMANDS:
                builder.count_command(command_name, chunk_index, timestamp, duration)
            else:
                builder._last_chunk_index = chunk_index
                builder._last_trace_end = trace_end

            elem.clear()

    tree_root = builder.finish()
    return {
        "driver": header.get("driver"),
        "timebase": header.get("timebase"),
        "root": tree_root,
    }


def build_summary(input_path: Path, renderdoc_info: dict[str, Any], parsed: dict[str, Any]) -> dict[str, Any]:
    root = parsed["root"]
    return {
        "schemaVersion": 1,
        "capturePath": str(input_path.resolve()).replace("\\", "/"),
        "renderDoc": {
            **renderdoc_info,
            "convertFormat": "xml",
        },
        "capture": {
            "driver": parsed.get("driver"),
            "timebase": parsed.get("timebase"),
            "drawCount": root["drawCount"],
            "drawIndexedCount": root["drawIndexedCount"],
            "dispatchCount": root["dispatchCount"],
        },
        "topLevelPasses": root["children"],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="将 RenderDoc .rdc 离线转换为 pass_summary.json")
    parser.add_argument("--input", required=True, help="输入 .rdc 文件路径")
    parser.add_argument("--output", required=True, help="输出 pass_summary.json 路径")
    parser.add_argument("--renderdoccmd", help="renderdoccmd 可执行文件路径")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()

    if not input_path.exists():
        print(f"输入文件不存在: {input_path}", file=sys.stderr)
        return 1

    renderdoccmd = resolve_renderdoccmd(args.renderdoccmd)
    renderdoc_info = detect_renderdoc_version(renderdoccmd)

    output_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="rdc_pass_summary_") as temp_dir:
        xml_path = Path(temp_dir) / f"{input_path.stem}.xml"
        convert_rdc_to_xml(renderdoccmd, input_path, xml_path)
        parsed = parse_xml_to_summary(xml_path)

    summary = build_summary(input_path, renderdoc_info, parsed)
    output_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Wrote pass summary to {str(output_path).replace('\\', '/')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
