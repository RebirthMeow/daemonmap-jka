# AGENTS.md

*This document was written entirely by AI (Anthropic's Claude) based on automated codebase analysis, with maintainer review. Mistakes that slipped through review are possible — please file an issue if you spot one.*

Orientation for AI coding tools (Claude Code, Cursor, Aider, Copilot, etc.) working in this fork.

## What this repo is

A fork of [DaemonEngine/daemonmap](https://github.com/DaemonEngine/daemonmap) (which is itself a `q3map2` fork from NetRadiant) extended to generate Recast/Detour navmeshes for **Jedi Academy** (`.bsp` RBSP v1) maps.

The upstream daemonmap was deprecated by Unvanquished in 2023 — the navmesh logic moved into the game engine. This fork resurrects daemonmap as an offline navmesh compiler tuned for JKA's physics, content flags, and movement abilities (drops, jumps, wallruns, jumppads, elevators).

The output binary `.navmesh` is consumed by [Bot2](https://github.com/RebirthMeow/Bot2), an OpenJK bot mod (`g_navmesh.cpp:95` opens `maps/<mapname>.navmesh`).

## Working principle: minimal footprint

Mirrors the convention from the Bot2 repo. Keep changes contained:

- All JKA-specific code lives in `tools/quake3/q3map2/`. Don't touch other directories unless absolutely necessary.
- Within `q3map2/`, prefer adding new files over editing stock upstream ones. New files in this fork: `bspfile_rbsp.c`, `game_ja.h`. Modified stock files: `nav.cpp` (heavily extended), `main.c`, `help.c`, `q3map2.h`, `path_init.c`, `navgen.h`, `CMakeLists.txt` — keep the deltas tight and well-commented.
- The Recast/Detour submodule (`libs/recastnavigation`) carries only build-system tweaks. **Do not patch Recast algorithms**; do tuning in `nav.cpp`'s Recast config setup.

## Where the architecture is documented

- `tools/quake3/q3map2/jka_navmesh_README.md` — full subsystem README: file map, function inventory, CLI flags, output formats, JKA Recast tuning, off-mesh inference passes. **Read this before editing `nav.cpp`.**
- `docs/nav_connections_format.md` — exact spec of the `.nav_connections` text sidecar (input to daemonmap, output of Bot2's headless wallrun scanner).
- `README.md` — project-level overview and build instructions.

## Style conventions

- Commit message prefix: `Daemonmap: <action>` (one logical step per commit). Mirrors Bot2's `Bot2: <action>`.
- AI-authored docs: italic disclaimer at top, e.g. *"This document was written entirely by AI (Anthropic's Claude)…"* — same wording as this file's preamble.
- Before non-trivial changes: ask the maintainer (use the `AskUserQuestion` tool if available). The user prefers terse, direct answers and clarifying questions before big edits.
- Verification: at decision points, dispatch a verification subagent (e.g. `Explore` or `general-purpose`) to confirm claims against the code before reporting a step as done.

## Build & verify

The user builds on Windows (MSYS2 + MinGW-w64); the canonical build script is `build.ps1`. Linux build via `build-msys2.sh` or the standard CMake workflow. The Linux sandbox cannot run the Windows binary, so trust the build loop + verification subagents — don't claim a change works without compile evidence.

## Data flow — iterative wallrun loop

The wallrun-aware navmesh build is a **two-pass loop** because the Bot2 wallrun
scanner needs a navmesh to walk on before it can validate wallrun candidates.

```
                        ┌───────────────────────────────────────────────┐
                        │                                               │
                        │              PASS 1 (initial)                 │
                        │                                               │
   ┌────────────┐       │   ┌──────────────┐         ┌──────────────┐   │
   │  JKA .bsp  │──────▶│──▶│  daemonmap   │────────▶│   .navmesh   │   │
   └────────────┘       │   │  -game ja    │         │  (drops +    │   │
                        │   │  -nav        │         │   jumps,     │   │
                        │   └──────────────┘         │   no wall-   │   │
                        │                            │   runs yet)  │   │
                        │                            └──────┬───────┘   │
                        └────────────────────────────────── │ ──────────┘
                                                            │
                                                            ▼
                            ┌─────────────────────────────────────────┐
                            │   Bot2 loads it (g_navmesh.cpp:95):     │
                            │     /bot_scan_wallruns   (headless)     │
                            │   ai_bot2_wallrun.c probes vertical     │
                            │   walls, writes hits to:                │
                            │     maps/<mapname>.nav_connections      │
                            └────────────────────┬────────────────────┘
                                                 │
                        ┌────────────────────────┼──────────────────────┐
                        │                        │                      │
                        │              PASS 2 (final)                   │
                        │                        ▼                      │
   ┌────────────┐       │   ┌──────────────┐         ┌──────────────┐   │
   │  JKA .bsp  │──────▶│──▶│  daemonmap   │────────▶│   .navmesh   │   │
   └────────────┘       │   │  -game ja    │  reads  │  (drops +    │   │
                        │   │  -nav        │  side-  │   jumps +    │   │
   ┌────────────┐       │   │              │  car    │   wallruns)  │   │
   │ .nav_conn  │──────▶│──▶│              │         │              │   │
   └────────────┘       │   └──────────────┘         └──────┬───────┘   │
                        │                                   │           │
                        └─────────────────────────────────── │ ──────────┘
                                                             │
                                                             ▼
                            ┌─────────────────────────────────────────┐
                            │   Bot2 loads final navmesh in-game;     │
                            │   bots now perform wallruns in combat.  │
                            └─────────────────────────────────────────┘
```

Key contract:
- **`.navmesh`** is the binary Recast/Detour output produced by daemonmap
  and consumed by Bot2 (`g_navmesh.cpp:95` reads `maps/<mapname>.navmesh`).
- **`.nav_connections`** is text. **Input** to daemonmap (optional sidecar),
  **output** of Bot2's `Bot2_ScanWallruns` (`ai_bot2_wallrun.c`).
- Pass 1 is sufficient for maps without wallrun-able geometry. Maps with
  vertical climbable walls need Pass 2 to actually unlock those moves.
- Daemonmap also auto-detects drop/jump/wallrun candidates from BSP
  geometry on every pass; the sidecar augments the auto-detection with
  scanner-validated or hand-authored entries.

## Quirks to know

- Coordinate handedness: Quake (Z-up) ↔ Recast (Y-up). Conversion in `TransformPointToRecast()` / `TransformPointToGame()`.
- Default unit scaling: meters (×0.0254). JKA mode uses `-meters` and `-solomesh` by default; turn them off for Unvanquished-compatible output.
- The `test map/` directory is gitignored except for one sample BSP (`test map/maps/mp/ctf_kejim.bsp`). Users supply their own JKA maps for testing — do not add Raven Software stock assets to the repo.
- `bspfile_rbsp.c` parses RBSP v1 (JKA / JK2). Earlier `bspfile_*` files in the same directory cover IBSP variants. JKA's RBSP is **not** Quake 3 BSP; struct sizes and lump counts differ.
