#!/usr/bin/env python3
"""subagentStart gate: every dispatch must declare resume-or-fresh.

Rule source: .agent/skills/subagent-delegation/SKILL.md ("按线复用").
The hook payload does not say whether Task got `resume`, so the dispatch
prompt must carry one declaration line:

    派发：续接 <agent-id>
    派发：新开 <类别> -- <一句话说明>

续接: <agent-id> must be a subagent that exists in this conversation
(its transcript is under agent-transcripts/<conversation>/subagents/).
An id owned by another conversation is denied too: Cursor rejects it with
"Subagent parent mismatch". `transcript_path` is empty in side chats, so
the lookup scans every project transcript root instead of trusting it.
新开: category is one of 新线 | 换模型 | 方案推翻 | 上下文过长 | 断线.
Read-only single-shot types (explore, browser-use, ...) are exempt.
No declaration, a made-up id, or an unknown category is denied.
"""
import glob
import json
import os
import re
import sys
import time

EXEMPT_TYPES = {
    "explore",
    "browser-use",
    "bugbot",
    "security-review",
    "cursor-guide",
    "ci-investigator",
    "best-of-n-runner",
}

CATEGORIES = ("新线", "换模型", "方案推翻", "上下文过长", "断线")
RESUME_RE = re.compile(r"派发\s*[:：]\s*续接\s+([0-9a-fA-F-]{36})")
FRESH_RE = re.compile(r"派发\s*[:：]\s*新开\s+(" + "|".join(CATEGORIES) + r")")

LOG = "/tmp/ya-subagent-gate.log"


def log(message):
    try:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%F %T')} {message}\n")
    except OSError:
        pass


VIOLATIONS_DIR = "/tmp/ya-subagent-violations"


def subagent_owners(agent_id, transcript_path):
    """Conversation ids whose transcripts hold this subagent.

    `transcript_path` is empty in side chats, so it cannot be the only anchor:
    also scan every project's agent-transcripts directory (ids are UUIDs).
    """
    roots = set(glob.glob(os.path.expanduser("~/.cursor/projects/*/agent-transcripts")))
    if transcript_path:
        roots.add(os.path.dirname(os.path.dirname(transcript_path)))
    owners = set()
    for root in roots:
        for hit in glob.glob(os.path.join(root, "*", "subagents", agent_id + ".jsonl")):
            owners.add(os.path.basename(os.path.dirname(os.path.dirname(hit))))
    return owners


def record_violation(payload, note):
    """Cursor 3.23 starts the subagent even when subagentStart denies, so the
    denial is also recorded for subagent-reuse-followup.py (the `stop` hook)."""
    try:
        os.makedirs(VIOLATIONS_DIR, exist_ok=True)
        path = os.path.join(VIOLATIONS_DIR, str(payload.get("conversation_id") or "unknown") + ".jsonl")
        # Cursor invokes the hook twice per dispatch; count one dispatch once.
        call_id = payload.get("tool_call_id") or payload.get("subagent_id")
        if call_id and os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                if any(json.loads(line).get("call_id") == call_id for line in f if line.strip()):
                    return
        with open(path, "a", encoding="utf-8") as f:
            f.write(json.dumps({
                "call_id": call_id,
                "subagent_type": payload.get("subagent_type"),
                "task": (payload.get("task") or "")[:120],
                "note": note,
            }, ensure_ascii=False) + "\n")
    except OSError:
        pass


def decide(payload):
    """Return (allowed, note). `note` explains a denial."""
    subagent_type = payload.get("subagent_type") or ""
    if subagent_type in EXEMPT_TYPES:
        return True, f"exempt type={subagent_type}"

    task = payload.get("task") or ""

    resume = RESUME_RE.search(task)
    if resume:
        agent_id = resume.group(1).lower()
        owners = subagent_owners(agent_id, payload.get("transcript_path") or "")
        if not owners:
            return False, (
                f"声明续接的 agent {agent_id} 在任何会话的 subagents/ 下都找不到。"
                "检查 id，或改成「派发：新开 <类别>」并说明理由。"
            )
        current = payload.get("conversation_id") or ""
        if current and current not in owners:
            return False, (
                f"agent {agent_id} 属于会话 {', '.join(sorted(owners))}，当前会话 {current} 不能续接它"
                "（Cursor 会报 parent mismatch）。在当前会话里新开，或回到那个会话续接。"
            )
        return True, f"resume {agent_id}"

    fresh = FRESH_RE.search(task)
    if fresh:
        return True, f"fresh reason={fresh.group(1)}"

    return False, "prompt 里没有「派发：」声明行。"


def main():
    payload = json.load(sys.stdin)
    allowed, note = decide(payload)
    log(f"{'allow' if allowed else 'DENY '} type={payload.get('subagent_type')!r} {note}")
    if allowed:
        print(json.dumps({"permission": "allow"}))
        return
    record_violation(payload, note)
    print(json.dumps({
        "permission": "deny",
        "user_message": "拦截派发：" + note,
        "agent_message": (
            note + " 先查「线 → agent id」表（.agent/skills/subagent-delegation/SKILL.md 按线复用）。"
            "同一条线上有已结束的 subagent，就用 resume 续接，并在 prompt 里单独写一行："
            "「派发：续接 <agent-id>」。只有这些情况才新开：" + " / ".join(CATEGORIES)
            + "，写一行「派发：新开 <类别> -- <一句话说明>」。"
        ),
    }, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # malformed payload: fail closed (non-2 exit + failClosed)
        log(f"ERROR {type(exc).__name__}: {exc}")
        sys.exit(1)
