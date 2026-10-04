#!/bin/bash
# Exclusive build lock for parallel agents / worktrees sharing one build tree.
#
#   Script/agent/ya_lock.sh run -- <command...>   wait for the lock, run, release
#   Script/agent/ya_lock.sh status                who holds it, doing what, for how long
#
# The lock is a directory (mkdir is atomic) holding the owner's pid plus a few
# info files, so a waiting agent can see who it is waiting for. A dead owner
# (killed shell, crashed agent) is detected by pid and its lock is reclaimed.
#
# Environment:
#   YA_AGENT_NAME            label shown to others (default: $USER)
#   YA_BUILD_LOCK_DIR        lock path (default /tmp/ya-build.lock.d)
#   YA_BUILD_LOCK_TIMEOUT    seconds to wait before giving up, exit 75 (default 3600)
#   YA_BUILD_LOCK_HELD=1     set inside the lock; nested `run` calls run directly
#
# Hold it only for commands that finish on their own (build, test, a run with
# --exit-after-frame). Never wrap a long-lived process.

set -u

LOCK="${YA_BUILD_LOCK_DIR:-/tmp/ya-build.lock.d}"
TIMEOUT="${YA_BUILD_LOCK_TIMEOUT:-3600}"
OWNER="${YA_AGENT_NAME:-${USER:-unknown}}"

holder_alive() {
    [ -f "$LOCK/pid" ] && kill -0 "$(cat "$LOCK/pid" 2>/dev/null)" 2>/dev/null
}

describe_holder() {
    local pid owner cmd since now
    pid="$(cat "$LOCK/pid" 2>/dev/null || echo '?')"
    owner="$(cat "$LOCK/owner" 2>/dev/null || echo 'unknown (older wrapper)')"
    cmd="$(cat "$LOCK/cmd" 2>/dev/null || echo '?')"
    since="$(cat "$LOCK/since" 2>/dev/null || echo 0)"
    now="$(date +%s)"
    if [ "$since" -gt 0 ] 2>/dev/null; then
        echo "held by '$owner' (pid $pid) for $((now - since))s: $cmd"
    else
        echo "held by '$owner' (pid $pid): $cmd"
    fi
}

cmd_status() {
    if [ -d "$LOCK" ] && holder_alive; then
        describe_holder
        return 1
    fi
    echo "free"
    return 0
}

cmd_run() {
    if [ "${1:-}" = "--" ]; then shift; fi
    if [ $# -eq 0 ]; then
        echo "usage: $0 run -- <command...>" >&2
        return 64
    fi
    if [ "${YA_BUILD_LOCK_HELD:-}" = "1" ]; then
        "$@"
        return $?
    fi

    local waited=0 announced=-1
    while ! mkdir "$LOCK" 2>/dev/null; do
        if ! holder_alive; then
            # Owner is gone; reclaim. A racing reclaimer simply loses the mkdir.
            rm -rf "$LOCK"
            continue
        fi
        if [ $((waited / 30)) -ne "$announced" ]; then
            announced=$((waited / 30))
            echo "[ya_lock] waiting ${waited}s: $(describe_holder)" >&2
        fi
        if [ "$waited" -ge "$TIMEOUT" ]; then
            echo "[ya_lock] gave up after ${waited}s" >&2
            return 75
        fi
        sleep 3
        waited=$((waited + 3))
    done

    echo $$ > "$LOCK/pid"
    echo "$OWNER" > "$LOCK/owner"
    echo "$*" > "$LOCK/cmd"
    date +%s > "$LOCK/since"
    trap 'rm -rf "$LOCK"' EXIT
    trap 'exit 130' INT TERM

    if [ "$waited" -gt 0 ]; then
        echo "[ya_lock] acquired after ${waited}s" >&2
    fi
    YA_BUILD_LOCK_HELD=1 "$@"
    return $?
}

case "${1:-}" in
    run)    shift; cmd_run "$@"; exit $? ;;
    status) cmd_status; exit $? ;;
    *)
        echo "usage: $0 {run -- <command...> | status}" >&2
        exit 64
        ;;
esac
