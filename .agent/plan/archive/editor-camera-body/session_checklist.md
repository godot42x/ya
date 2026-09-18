# Session Checklist

## Open a session

1. Read root `AGENTS.md`, then `.agent/skills/ya-build/SKILL.md` for build/run.
2. Read `.agent/skills/scene-object-boundary/SKILL.md` before touching anything
   that generates or owns entities.
3. `git status --porcelain` — this repo usually has concurrent module work in
   flight. Never `git checkout .` / `git stash`; identify your own paths first.
4. Build the narrowest target that covers the change (`ya-render-ecs-adapters`
   for companions, `ya-game-runtime` for wiring, `ya-game-editor` for editor
   surfaces, `ya-testing` for tests).

## Verify a companion change

1. `./build/macosx/arm64/debug/ya-testing --gtest_filter='LinkageFrameworkTest.*:SceneSerializerTest.*'`
2. Check the four consumer paths by hand when the change touches them:
   serialization (scene file stays clean), extraction (`features` bits land on
   the right buckets), picking (hit resolves to the host), inspector (fields
   greyed for a companion).
3. Confirm no `Entity::getComponent<T>()` was added on a path where `T` may be
   absent — it asserts. Use `hasComponent` first.

## Close a session

1. `git diff --stat` and map every changed path to a plan checkpoint; unplanned
   files are other agents' work — leave them alone.
2. Update `progress.md` (what landed, what was verified, what is open) and
   `feature_matrix.json` states.
3. Report honestly: name pre-existing failures and unrun manual checks instead of
   implying they passed.
