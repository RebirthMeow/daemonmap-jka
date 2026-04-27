# JKA NavMesh Subsystem

*This document was written entirely by AI (Anthropic's Claude) based on automated codebase analysis, with maintainer review. Mistakes that slipped through review are possible — please file an issue if you spot one.*

Architectural reference for the Jedi Academy navmesh extension to daemonmap. Read before editing `nav.cpp`, `bspfile_rbsp.c`, or `game_ja.h`.

## What this subsystem does

Given a JKA `.bsp` (RBSP v1), produces a Recast/Detour binary navmesh (`.navmesh`) that Bot2 loads at runtime. Auto-detects walk-off drops, running jumps, and vertical wallruns from BSP geometry. Reads an optional `.nav_connections` text sidecar for hand-authored or scanner-validated connections (jumppads, elevators, additional wallruns).

The Bot2 wallrun scanner produces the `.nav_connections` sidecar at runtime, which means a wallrun-aware build is **two-pass**: build the initial navmesh, run the in-game scanner against it, rebuild with the resulting sidecar. See `AGENTS.md` § "Data flow" for the diagram.

## File map

All paths relative to `tools/quake3/q3map2/`.

| File | Status | Purpose |
|---|---|---|
| `nav.cpp` | heavily extended | Main navmesh build pipeline. ≈2300 lines added on top of upstream. |
| `bspfile_rbsp.c` | new | RBSP v1 BSP loader for JKA / JK2. 18 lumps. |
| `game_ja.h` | new | JKA game definition: physics constants, content/surface flag tables, shader-prefix → flag rules. |
| `main.c` | +28 lines | Registers the JKA game and CLI flags. |
| `help.c` | +11 lines | Help-text additions for new `-nav` flags. |
| `q3map2.h` | +18 lines | Shared declarations for RBSP loader and JKA game. |
| `path_init.c` | +2 lines | Path-init hook for JKA game. |
| `navgen.h` | +2 lines | Forward decls for sidecar loader. |
| `CMakeLists.txt` | +15 lines | Build rules for the new sources. |

## Dependency graph

```
              ┌──────────────┐
              │   main.c     │  CLI dispatch
              └───────┬──────┘
                      │
              ┌───────▼──────┐
              │  nav.cpp     │
              │  NavMain()   │
              └─┬─────┬────┬─┘
                │     │    │
       ┌────────┘     │    └──────────────┐
       │              │                   │
┌──────▼──────┐ ┌─────▼──────┐ ┌──────────▼──────────┐
│bspfile_rbsp │ │ game_ja.h  │ │ libs/recastnavigation│
│ (RBSP load) │ │ (constants)│ │ (Recast + Detour)    │
└─────────────┘ └────────────┘ └─────────────────────┘
```

`nav.cpp` is the orchestrator. It calls into RBSP for geometry, reads tuning from `game_ja.h`, drives Recast/Detour for the actual build, and emits binary navmesh + reads the optional sidecar.

## CLI surface

Invocation:

```
daemonmap -game ja -nav <path/to/map.bsp> [flags]
```

JKA-specific flags (all optional; sane defaults for JKA are pre-applied):

| Flag | Type | Default | Purpose |
|---|---|---|---|
| `-cellheight <F>` | float | `2.0` | Recast voxel height in Quake units. Lower = finer mesh, more memory. |
| `-stepsize <F>` | float | `18.0` | Walkable step / climb height in Quake units (matches JKA's stair tolerance). |
| `-includecaulk` | flag | off | Include caulk surfaces in the navmesh. Default excludes them. |
| `-includesky` | flag | off | Include sky surfaces. Default excludes them. |
| `-nogapfilter` | flag | off | Disable the custom Recast gap-filler that fills micro-gaps with walkable spans. |
| `-meters` | flag | **on** for `-game ja` | Scale to meters (×0.0254) internally. Recast operates better in meter-scale units. |
| `-solomesh` | flag | **on** for `-game ja` | Solo-tile build → emit `<map>.navmesh` (Bot2-compatible). Off → tiled build → emit `<map>-jka.navMesh` (Unvanquished format). |

`daemonmap -help nav` lists all options at runtime.

## Output formats

### `.navmesh` (solo mesh — JKA default)

Binary, written by `WriteSoloNavMeshFile()` (`nav.cpp:886`). Same directory as the input BSP, with `.bsp` replaced by `.navmesh`. The extension matches Bot2's runtime loader (`g_navmesh.cpp:95` opens `maps/<mapname>.navmesh`).

| Offset | Bytes | Field |
|---|---|---|
| 0 | 4 | Magic `MSET` (0x4D534554) |
| 4 | 4 | Version `1` |
| 8 | 4 | Tile count (always `1` in solo mode) |
| 12 | sizeof(`dtNavMeshParams`) | Detour navmesh params |
| ... | 8 | Tile reference |
| ... | 4 | Compressed tile data size |
| ... | size | Compressed tile data |

### `.navMesh` (tiled — Unvanquished-compatible, off by default)

Binary, written by `WriteNavMeshFile()` (`nav.cpp:145`). Path: `<mapname>-jka.navMesh`. Used when `-solomesh` is explicitly disabled. Header includes `dtTileCacheParams` and per-tile blocks. Format matches upstream Unvanquished daemonmap output.

### `.nav_connections` (input sidecar)

See `docs/nav_connections_format.md` for the full spec. Loaded by `Nav_LoadSidecarConnections()` (`nav.cpp:1796`) before the final navmesh bake.

## JKA Recast tuning

Constants defined in `nav.cpp` (with cross-references to `libs/unvanquished/src/sgame/botlib/nav.h` for inherited values) and `game_ja.h`.

### Agent

| Quantity | Value | Source |
|---|---|---|
| Agent radius | 15.0 u | `characterArray[0]` (`nav.cpp:125`) |
| Agent height | 64.0 u | same |
| Min walk normal | 0.7 (slope ≈ 45.6°) | `MIN_WALK_NORMAL` (nav.h) |
| Step size | 18.0 u | `STEPSIZE` (nav.h) |

### Voxel grid

| Quantity | Formula | Resulting value |
|---|---|---|
| `cellHeight` | configurable | 2.0 u (default) |
| `cellSize` | `radius / 4.0` | 3.75 u |
| `walkableHeight` | `ceil(height / ch)` | 32 voxels |
| `walkableClimb` | `floor(stepsize / ch)` | 9 voxels |
| `walkableRadius` | `ceil(radius / cs)` | 4 voxels |
| `walkableSlopeAngle` | `acos(0.7)` | ≈ 45.57° |

### Mesh simplification

| Param | Value |
|---|---|
| `maxSimplificationError` | 1.3 |
| `minRegionArea` | 16 voxels (`rcSqr(4)`) |
| `mergeRegionArea` | 400 voxels (`rcSqr(20)`) |
| `maxVertsPerPoly` | 6 |
| `maxEdgeLen` | 12 m / cs (≈ 127 voxels in meters mode) |
| `detailSampleDist` | `cs × 6` |
| `detailSampleMaxError` | `ch × 1.0` |

### Off-mesh connection physics

| Quantity | Value | Notes |
|---|---|---|
| Min drop dist | 22 u | `STEPSIZE + 4` |
| Max drop dist | 370 u | impact-lethal fall threshold (≈769 u/s vertical) |
| Jump velocity | 270 u/s | JKA stock vertical takeoff |
| Jump max gain | 45.6 u | `JUMP_VEL² / (2·g)` with g=800 |
| Wallrun min height | 45 u | minimum climbable wall |
| Wallrun max height | 450 u | sustained wallrun cap (≈376 u elevator span + margin) |
| Connection activation radius | 24–32 u | varies per movement type |

## Function inventory (nav.cpp)

Grouped by stage. Helpers omitted; ~22 top-level functions.

### BSP → geometry

| Function | Line | Role |
|---|---|---|
| `LoadGeometry()` | 402 | Load brush + patch triangles from BSP. |
| `LoadBrushTris()` | 247 | Tessellate solid brush sides. |
| `LoadPatchTris()` | 341 | Tessellate curved patches via `cm_patch.h`. |
| `AddVert()` | 219 | Append a vertex (with Quake→Recast transform). |
| `AddTri()` | 241 | Append a triangle index triplet. |

### Recast build core

| Function | Line | Role |
|---|---|---|
| `rcErodeWalkableAreaByBox()` | 430 | Erode agent radius from walkable spans. |
| `rcFilterGaps()` | 621 | Custom filter: fill micro-gaps with walkable spans. |
| `rasterizeTileLayers()` | 726 | Per-tile rasterization (used in tiled-mesh path). |

### Output

| Function | Line | Role |
|---|---|---|
| `WriteSoloNavMeshFile()` | 886 | Emit `.navmesh` (JKA default). |
| `WriteNavMeshFile()` | 145 | Emit `.navMesh` (Unvanquished tiled format). |

### Coordinate transforms

| Function | Line | Role |
|---|---|---|
| `TransformPointToRecast()` | 974 | Quake (Z-up) → Recast (Y-up); applies meters scaling. |
| `TransformPointToGame()` | 985 | Inverse, for log output. |

### Off-mesh connection inference

| Function | Line | Role |
|---|---|---|
| `Nav_TraceRayHit()` | 1012 | Ray-cast against the chunky triangle mesh. |
| `Nav_GetBoundaryEdges()` | 1082 | Extract navmesh poly boundary edges. |
| `Nav_DetectDropConnections()` | 1138 | **Pass 1:** walk-off ledges → `AREA_JUMP_DROP`. |
| `Nav_CheckLOS()` | 1257 | Line-of-sight check for jump feasibility. |
| `Nav_DetectJumpConnections()` | 1305 | **Pass 2:** running jumps across gaps → `AREA_JUMP_BASIC`. |
| `Nav_DetectWallrunConnections()` | 1478 | **Pass 3:** vertical wallruns → `AREA_WALLRUN_ASCEND`. |
| `Nav_LoadSidecarConnections()` | 1796 | Read `.nav_connections` sidecar; snap, dedup, elevator-nudge. |
| `ExtractOffMeshConnections()` | 1999 | Orchestrate: collect trigger volumes, run 3 passes, load sidecar. |

### Top-level

| Function | Line | Role |
|---|---|---|
| `BuildNavMesh()` | 2593 | Full pipeline: load → configure → rasterize → compact → regions → contours → polymesh → off-mesh → write. |
| `NavMain()` | 2958 | CLI dispatcher: parse flags, validate BSP, call `BuildNavMesh`. |

## Off-mesh connection inference (heuristics)

Three deterministic passes operate on the freshly-built navmesh polys before the final Detour bake. Each pass walks the boundary edges of every poly and tests candidate destinations.

**Pass 1 — Drop (`AREA_JUMP_DROP`):**
For each boundary edge, cast a ray straight down up to `MAX_DROP` (370 u). If a walkable poly is hit between `MIN_DROP` (22 u) and `MAX_DROP`, emit a one-way connection. Activation radius 24 u. Reject landings inside `trigger_hurt` volumes.

**Pass 2 — Basic Jump (`AREA_JUMP_BASIC`):**
For each boundary edge, attempt a ballistic launch at `JUMP_VEL` (270 u/s) vertical + horizontal approach. Solve the parabola for landing time and position. Accept landings within `MAX_JUMP_DROP` (48 u) elevation loss and `MAX_DROP_DIST` (370 u) horizontal distance, with LOS clear. Activation radius 32 u.

**Pass 3 — Wallrun Ascend (`AREA_WALLRUN_ASCEND`):**
Scan vertical wall segments (tall, narrow). Cast upward from the edge origin between `WALLRUN_MIN_H` (45 u) and `WALLRUN_MAX_H` (450 u). Test for a landable floor at the wall top with a small overshoot. Activation radius 32 u. **In practice this pass is conservative;** Bot2's runtime wallrun scanner produces the bulk of valid wallrun connections via the sidecar.

After all three passes, `Nav_LoadSidecarConnections` merges hand-authored / scanner-produced entries. A final geometric dedup pass removes near-duplicates regardless of source.

### Trigger-volume integration

Before the inference passes, daemonmap collects entity volumes from the BSP entities lump:

- `trigger_hurt` → reject as drop landing (lethal).
- `trigger_push` (jumppad) → emits multi-tier `POLYAREA_JUMPPAD` connections via clustered launch-trajectory analysis (`nav.cpp:2349`, `2442`).
- `func_door` / `func_plat` → register as elevator volumes; daemonmap emits `POLYAREA_ELEVATOR` connections at the floor and ceiling waiting positions, and uses these volumes for crush-avoidance during sidecar snapping.

## bspfile_rbsp.c

JKA / JK2 use the **RBSP v1** BSP format, which differs from Quake 3's IBSP:
- 4 lightmaps per face instead of 1 (`MAX_LIGHTMAPS = 4`).
- 4-tier color/style arrays per draw vert.
- 18 lumps including the JKA-specific advertisements lump (parsed but unused).

`LoadRBSPFile()` (`bspfile_rbsp.c:292`) validates the `RBSP` ident and version `1`, then copies each lump via dedicated `Copy*Lump()` routines that account for the alignment and lightmap-count differences. `WriteRBSPFile()` is a stub — daemonmap doesn't emit BSPs.

## game_ja.h

JKA's content / surface flag bitfields are similar to Quake 3 but not identical (e.g. `JA_CONT_BOTCLIP = 0x400000`, `JA_CONT_JUMPPAD = 0x80000`, `JA_SURF_LADDER = 0x8`). The header defines the full bitfield enums and a shader-prefix → flag rules table that maps texture name patterns (e.g. `water`, `trigger`, `origin`) to the compiler's content / surface flag overrides.

## Submodule note

`libs/recastnavigation` carries only build-system tweaks (CMake modernization, CI config, editor settings) on top of upstream Recast. **No algorithm patches.** All JKA-specific tuning lives in `nav.cpp`'s Recast config setup, not in the submodule.
