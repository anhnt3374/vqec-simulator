# CLAUDE.md

@AGENTS.md

## Claude Code notes

- All project rules live in `AGENTS.md` (imported above). Update that file, not this one, when a
  rule changes; keep this file for Claude Code–specific workflow only.
- Run every build, test and check through `scripts/dev.sh <command>`; it runs non-interactively
  when given a command. Never call `cmake`, `g++` or `ctest` directly on the host.
- Iterate with the `cpu-debug` preset. Before reporting a task as done, run
  `scripts/dev.sh cmake --build --preset cpu-debug`, `scripts/dev.sh ctest --preset cpu-debug`
  and `scripts/dev.sh scripts/check.sh`, and state the results.
- The first `scripts/dev.sh` run builds the dev image (several minutes). Run it in the
  background instead of polling.
- GPU and NPU paths cannot be verified on a machine without that hardware; say so explicitly
  instead of claiming they work.
