#!/usr/bin/env python3
"""YA Engine MCP bridge + remote-exec client.

Speaks MCP (Model Context Protocol, JSON-RPC 2.0 over stdio) on stdin/stdout
and forwards every tool call to a running engine's automation control server
(TCP, line-delimited JSON). The tool catalog is generated live from the
engine's `list_commands`, so a command registered once appears as an MCP tool,
as a JS library function (`ya.<ns>.<fn>`) and as an RPC call.

Usage:
    # MCP server mode (pipe into Claude Desktop / Cursor / custom clients):
    python3 Script/ya_mcp_bridge.py --port 8123

    # Direct remote-exec mode (UE "Execute Python" style, no MCP needed):
    python3 Script/ya_mcp_bridge.py --port 8123 --call eval_js '{"source": "ya.scene.active().entityCount()"}'
    python3 Script/ya_mcp_bridge.py --port 8123 --call asset.get_info '{"path": "Models/Box.fbx"}'

Start the engine with the automation server enabled:
    python3 Script/ya.py run-editor --project ... -- --automation-control-port=8123
"""

from __future__ import annotations

import argparse
import json
import socket
import sys

ENGINE_RPC_TIMEOUT_S = 30


def rpc_call(host: str, port: int, method: str, params: dict | None = None) -> dict:
    """One line-delimited JSON-RPC request to the engine automation server."""
    request = {"method": method, "id": 1, "params": params or {}}
    payload = (json.dumps(request) + "\n").encode("utf-8")

    with socket.create_connection((host, port), timeout=ENGINE_RPC_TIMEOUT_S) as sock:
        sock.sendall(payload)
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                raise RuntimeError("engine closed the automation connection")
            data += chunk

    response = json.loads(data.decode("utf-8"))
    if not response.get("ok"):
        raise RuntimeError(response.get("error", "engine rpc failed"))
    return response.get("result", {})


def _command_tools(host: str, port: int) -> tuple[list[dict], dict[str, str]]:
    commands = rpc_call(host, port, "list_commands").get("commands", [])
    tools = []
    mcp_name_to_command = {}
    for command in commands:
        name = command.get("name", "")
        if not name:
            continue
        mcp_name = name.replace(".", "_")
        mcp_name_to_command[mcp_name] = name
        arg_schema = command.get("args") or {}
        properties = {}
        required = []
        for key, spec in arg_schema.items():
            properties[key] = {"type": spec.get("type", "string")} if isinstance(spec, dict) else {"type": "string"}
        tools.append({
            "name": mcp_name,
            "description": command.get("doc", "") + f"\n\nEngine command: `{name}` (params: {json.dumps(arg_schema)})",
            "inputSchema": {
                "type": "object",
                "properties": properties,
                "required": required,
            },
        })
    mcp_name_to_command["eval_js"] = "eval_js"
    tools.append({
        "name": "eval_js",
        "description": "Evaluates a JavaScript snippet inside the engine's embedded quickjs runtime "
                       "and returns the last expression's value as JSON. "
                       "Use ya.entity/ya.scene/ya.<library> objects to manipulate the engine.",
        "inputSchema": {
            "type": "object",
            "properties": {"source": {"type": "string"}},
            "required": ["source"],
        },
    })
    return tools, mcp_name_to_command


def _text_result(result) -> dict:
    return {
        "content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}],
        "isError": False,
    }


def _error_result(message: str) -> dict:
    return {
        "content": [{"type": "text", "text": message}],
        "isError": True,
    }


def _mcp_response(request_id, result=None, error=None) -> dict:
    response = {"jsonrpc": "2.0", "id": request_id}
    if error is not None:
        response["error"] = {"code": error[0], "message": error[1]}
    else:
        response["result"] = result if result is not None else {}
    return response


def run_mcp_server(host: str, port: int) -> None:
    """MCP stdio server: newline-delimited JSON-RPC 2.0 on stdin/stdout."""
    server_info = {"name": "ya-engine-mcp", "version": "0.1.0"}
    mcp_name_to_command = {}

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            # MCP has no error channel for unparseable frames; drop them.
            continue

        method = message.get("method")
        request_id = message.get("id")
        is_notification = request_id is None

        try:
            if method == "initialize":
                response = _mcp_response(request_id, {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": server_info,
                })
            elif method == "notifications/initialized" or method == "notifications/cancelled":
                continue
            elif method == "ping":
                response = _mcp_response(request_id, {})
            elif method == "tools/list":
                tools, mcp_name_to_command = _command_tools(host, port)
                response = _mcp_response(request_id, {"tools": tools})
            elif method == "tools/call":
                params = message.get("params") or {}
                try:
                    mcp_name = params.get("name", "")
                    command_name = mcp_name_to_command.get(mcp_name)
                    if command_name is None:
                        raise RuntimeError(f"unknown tool: {mcp_name}")
                    if command_name == "eval_js":
                        result = rpc_call(host, port, "eval_js",
                                          {"source": (params.get("arguments") or {}).get("source", "")}).get("result")
                    else:
                        result = rpc_call(host, port, "invoke",
                                          {"name": command_name, "args": params.get("arguments") or {}}).get("result")
                    response = _mcp_response(request_id, _text_result(result))
                except (RuntimeError, OSError, json.JSONDecodeError) as exc:
                    response = _mcp_response(request_id, _error_result(str(exc)))
            else:
                response = _mcp_response(request_id, error=(-32601, f"Method not found: {method}"))
        except (RuntimeError, OSError, json.JSONDecodeError) as exc:
            response = _mcp_response(request_id, _error_result(str(exc)))

        if not is_notification and response is not None:
            sys.stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
            sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="engine automation server host")
    parser.add_argument("--port", type=int, default=8123, help="engine automation control port")
    parser.add_argument("--call", nargs=2, metavar=("METHOD", "ARGS_JSON"),
                        help="remote-exec mode: METHOD ARGS_JSON, print the result JSON and exit")
    args = parser.parse_args()

    if args.call:
        method, args_json = args.call
        params = json.loads(args_json) if args_json else {}
        if method == "eval_js":
            result = rpc_call(args.host, args.port, "eval_js", params).get("result")
        else:
            result = rpc_call(args.host, args.port, "invoke", {"name": method, "args": params}).get("result")
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0

    run_mcp_server(args.host, args.port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
