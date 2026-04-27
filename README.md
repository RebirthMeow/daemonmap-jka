# daemonmap-jka

*This README, along with the other in-repo docs (`AGENTS.md`, `tools/quake3/q3map2/jka_navmesh_README.md`, `docs/nav_connections_format.md`), was written entirely by AI (Anthropic's Claude) based on automated codebase analysis, with maintainer review. Mistakes that slipped through review are possible — please file an issue if you spot one.*

Offline navmesh compiler for **Jedi Academy** maps. Produces a Recast/Detour binary `.navmesh` from a JKA `.bsp`, consumed at runtime by [Bot2](https://github.com/RebirthMeow/Bot2).

This is a fork of [DaemonEngine/daemonmap](https://github.com/DaemonEngine/daemonmap) (which was deprecated upstream when Unvanquished moved navmesh generation in-engine). This fork resurrects the tool and tunes it for JKA's RBSP v1 format, content flags, agent dimensions, and movement abilities (drops, jumps, wallruns, jumppads, elevators).

**Quickest path (Windows, after building or downloading a release):**

For a basic navmesh (drops + jumps, no wallruns) — one drag:

> Drag your `.bsp` file onto `daemonmap-jka.bat`. A `.navmesh` appears next to it. Copy that `.navmesh` into your JKA `base\maps\` folder. Bots can navigate the map.

For a full navmesh with wallruns — two drags + an in-game step in between, see the [step-by-step below](#full-workflow-including-wallruns).

Or from a shell:

```
daemonmap -game ja -nav path/to/map.bsp
# produces path/to/map.navmesh
```

---

## What's in this fork vs upstream

JKA-specific work concentrates in `tools/quake3/q3map2/`:

- **`nav.cpp`** (heavily extended) — agent dimensions retuned for JKA, three-pass off-mesh connection auto-detection (drops, jumps, wallruns), clustered jumppad / elevator handling, optional `.nav_connections` text sidecar reader, JKA `.nav` binary output format.
- **`bspfile_rbsp.c`** (new) — RBSP v1 BSP loader. JKA's BSP format is not Quake 3's IBSP; this handles the 18-lump layout, 4-lightmap draw verts, and the JKA-specific advertisements lump.
- **`game_ja.h`** (new) — JKA content / surface flag bitfields and shader-prefix → flag rules table.
- Surrounding integration: registration in `main.c`, help text in `help.c`, declarations in `q3map2.h`, build rules in `CMakeLists.txt`.
- **`build.ps1`** / **`build-msys2.sh`** — Windows MSYS2/MinGW-w64 build scripts.

The Recast/Detour submodule (`libs/recastnavigation`) carries only build-system tweaks; no algorithm patches.

## For non-technical users

Don't want to build from source? Grab the latest pre-built Windows binary from the **[Releases page](https://github.com/RebirthMeow/daemonmap-jka/releases)** — download the `daemonmap-jka-vX.Y.Z-windows.zip`, unzip it anywhere, and drag a `.bsp` onto `daemonmap-jka.bat` inside the unzipped folder. The `.navmesh` lands next to your `.bsp`; copy it into JKA's `base\maps\` folder so Bot2 can find it.

## Building

### Windows (MSYS2 + MinGW-w64)

Requires [MSYS2](https://www.msys2.org/) installed at one of the standard locations (`C:\msys64`, etc.). The build script handles the rest.

```powershell
.\build.ps1
```

Output: `install\daemonmap.exe` plus required DLLs alongside it. Drop the `daemonmap-jka.bat` from the repo root into `install\` (or just leave it where it is — the wrapper finds the exe in either location).

### Packaging a release

Once you have a working build, package it for GitHub Releases:

```powershell
.\package-release.ps1 v1.0.0
```

Produces `release\daemonmap-jka-v1.0.0-windows.zip` — drag that into a new GitHub Release page and publish.

### Linux

Standard CMake workflow (same as upstream daemonmap):

```sh
git clone --recurse-submodules https://github.com/<your-fork>/daemonmap.git
cd daemonmap
cmake -G "Unix Makefiles" -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc) install
```

Or use the convenience script:

```sh
./build-msys2.sh   # also works under Linux despite the name
```

## Usage

Compile a navmesh for a JKA map:

```sh
daemonmap -game ja -nav path/to/map.bsp
```

Outputs `path/to/map.nav` next to the input. JKA mode defaults to solo-tile + meters-scale internal coordinates.

For full flag list:

```sh
daemonmap -help nav
```

JKA-specific flags include `-cellheight`, `-stepsize`, `-includecaulk`, `-includesky`, `-nogapfilter`, `-meters`, `-solomesh`. See `tools/quake3/q3map2/jka_navmesh_README.md` for what each does and the JKA Recast tuning rationale.

## Full workflow including wallruns

Daemonmap auto-detects drops and running jumps from BSP geometry on every run. **Wallruns** are different — they require a second pass through the toolchain because the Bot2 wallrun scanner needs an existing navmesh to walk on before it can validate climbable walls.

Concrete step-by-step (assumes you have a built `daemonmap.exe` and `daemonmap-jka.bat` next to it, and JKA installed at e.g. `C:\GameData\base\`):

1. **Pass 1 — initial navmesh.** Drag `mymap.bsp` onto `daemonmap-jka.bat`. It produces `mymap.navmesh` in the same folder as the `.bsp`. The bat will print `Sidecar: none` because there's no `.nav_connections` yet — that's expected on the first pass.
2. **Install the navmesh.** Copy `mymap.navmesh` into `C:\GameData\base\maps\`.
3. **Run Bot2's wallrun scanner.** Launch JKA with the Bot2 mod loaded, load `mymap`, open the console (`~`) and run:
   ```
   /bot_scan_wallruns
   ```
   The scanner runs headlessly (no visible bot), probes every vertical wall on the map, and writes hits to `C:\GameData\base\maps\mymap.nav_connections`.
4. **Move the sidecar back next to your source `.bsp`.** This is the easy-to-miss step. Daemonmap looks for `mymap.nav_connections` in the **same folder as the `mymap.bsp` you drag onto the bat**, not in `base\maps\`. So move (or copy) the file:
   ```
   from:  C:\GameData\base\maps\mymap.nav_connections
   to:    <wherever you keep mymap.bsp>\mymap.nav_connections
   ```
5. **Pass 2 — final navmesh.** Drag `mymap.bsp` onto `daemonmap-jka.bat` again. This time the bat prints `Sidecar found: ...mymap.nav_connections` and bakes the wallruns into a fresh `mymap.navmesh`.
6. **Re-install.** Copy the new `mymap.navmesh` over the old one in `C:\GameData\base\maps\`. Bots now perform wallruns in combat on this map.

Skip steps 3–6 entirely for maps without climbable walls — Pass 1 is enough.

## Pipeline diagram

```
                        ┌───────────────────────────────────────────────┐
                        │              PASS 1 (initial)                 │
   ┌────────────┐       │   ┌──────────────┐         ┌──────────────┐   │
   │  JKA .bsp  │──────▶│──▶│  daemonmap   │────────▶│   .navmesh   │   │
   └────────────┘       │   │  -game ja    │         │  drops +     │   │
                        │   │  -nav        │         │  jumps only  │   │
                        │   └──────────────┘         └──────┬───────┘   │
                        └────────────────────────────────── │ ──────────┘
                                                            ▼
                            ┌─────────────────────────────────────────┐
                            │  Bot2 loads .navmesh, you run:          │
                            │       /bot_scan_wallruns                │
                            │  Bot2 probes vertical walls headlessly  │
                            │  and writes hits to:                    │
                            │       <mapname>.nav_connections         │
                            └────────────────────┬────────────────────┘
                                                 │
                        ┌────────────────────────┼──────────────────────┐
                        │              PASS 2 (final)                   │
   ┌────────────┐       │                        ▼                      │
   │  JKA .bsp  │──────▶│   ┌──────────────┐         ┌──────────────┐   │
   └────────────┘       │   │  daemonmap   │────────▶│   .navmesh   │   │
   ┌────────────┐       │   │  -game ja    │  reads  │  drops +     │   │
   │ .nav_conn  │──────▶│──▶│  -nav        │  side-  │  jumps +     │   │
   └────────────┘       │   │              │  car    │  wallruns    │   │
                        │   └──────────────┘         └──────┬───────┘   │
                        └─────────────────────────────────── │ ──────────┘
                                                             ▼
                            ┌─────────────────────────────────────────┐
                            │  Bot2 loads final navmesh — bots now    │
                            │  perform wallruns in combat.            │
                            └─────────────────────────────────────────┘
```

For maps without climbable walls, Pass 1 is enough — skip the rest.

The `.nav_connections` sidecar is plain text. Bot2's scanner writes lines like:

```
# type      sx      sy      sz      ex      ey      ez     bidir  radius  area
WALLRUN   -512.0   256.0    32.0   -512.0   256.0   320.0    0     32.0    4
```

Daemonmap reads them on Pass 2, validates each entry against the navmesh (snap distance, height gain, elevator-crush avoidance), bakes them into the binary `.navmesh`. You can also hand-author entries — full spec at `docs/nav_connections_format.md`.

## Test maps

The `test map/` directory tracks **one** sample BSP (`maps/mp/ctf_kejim.bsp`) as a smoke-test fixture. All other contents — `.bsp`, `.nav`, `.pk3`, level shots, textures — are gitignored. Bring your own JKA maps for testing.

## Architecture deep dive

For the full subsystem README (file map, function inventory, JKA Recast tuning, off-mesh inference passes, RBSP format notes), see:

- `tools/quake3/q3map2/jka_navmesh_README.md` — subsystem-level architecture
- `docs/nav_connections_format.md` — sidecar format spec
- `AGENTS.md` — orientation for AI coding tools

## License

GPLv2 (inherited from upstream daemonmap and NetRadiant). See `GPL`, `LGPL`, and `LICENSE` files.

## Acknowledgments

- **DaemonEngine / Unvanquished** for the original daemonmap and the Recast integration this fork builds on. <https://github.com/DaemonEngine/daemonmap>
- **NetRadiant / Xonotic** for the q3map2 toolchain that daemonmap forked from. <https://gitlab.com/xonotic/netradiant>
- **Recast & Detour** by Mikko Mononen. <https://github.com/recastnavigation/recastnavigation>
- **Fuma** for the original navmesh computation code in upstream daemonmap.

This fork retains those copyrights and licenses; the JKA-specific additions are released under the same GPLv2.
