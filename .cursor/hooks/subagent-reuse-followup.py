#!/usr/bin/env python3
"""stop hook: turn a recorded dispatch violation into a forced follow-up.

subagent-reuse-gate.py denies undeclared dispatches, but Cursor 3.23 still
starts the subagent. So the denial is appended to
/tmp/ya-subagent-violations/<conversation>.jsonl and, when the agent tries to
end its turn, this hook hands the list back as `followup_message` once.
"""
import json
import os
import sys

VIOLATIONS_DIR = "/tmp/ya-subagent-violations"


def main():
    payload = json.load(sys.stdin)
    path = os.path.join(VIOLATIONS_DIR, str(payload.get("conversation_id") or "unknown") + ".jsonl")
    if not os.path.isfile(path):
        print("{}")
        return
    with open(path, encoding="utf-8") as f:
        records = [json.loads(line) for line in f if line.strip()]
    os.remove(path)
    if not records:
        print("{}")
        return
    lines = "\n".join(f"- {r['subagent_type']}: {r['note']} (task: {r['task']!r})" for r in records)
    print(json.dumps({
        "followup_message": (
            "派发门禁记录了 " + str(len(records)) + " 次违规（没有「派发：续接 <id>」/「派发：新开 <类别>」声明，"
            "或续接 id 不存在）：\n" + lines + "\n"
            "先向用户说明哪次派发违反了 .agent/skills/subagent-delegation/SKILL.md 的「按线复用」，"
            "再按线表补救：能续接的 worker 改用 resume，已经起来的按情况处理。"
        ),
    }, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        print("{}")
