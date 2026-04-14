# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

FlowX is a EuroScope plugin (Windows DLL) for VATSIM air traffic controllers. It assists delivery/ground/tower controllers with departure management, holding point assignments, and same-SID tracking. Currently configured primarily for LOWW (Vienna).

## Build

Open `FlowX.sln` in **Visual Studio 2022** and build with MSBuild. No CLI build commands are available.

- Output: 32-bit or 64-bit DLL (`Release|Win32` or `Release|x64`)
- Language standard: C++23
- EuroScope lib: `lib\EuroScope\EuroScopePlugInDll.lib`
- External headers: `include/` (nlohmann/json, semver, date/tz)

**Debug setup:** Set `EUROSCOPE_ROOT` environment variable to the EuroScope install directory. The debugger launches EuroScope.exe automatically. Debug mode is toggled via the plugin's right-click menu.

## Architecture

The plugin uses a deep inheritance chain (bottom to top):

```
EuroScopePlugIn::CPlugIn
  └─ CFlowX_Base       (radar screen creation and plugin registration calls)
       └─ CFlowX_Logging    (chat message logging)
            └─ CFlowX_Settings  (config.json + EuroScope settings)
                 └─ CFlowX_LookupsTools  (geometry: point-in-polygon, distances)
                      └─ CFlowX_Timers   (aircraft state maps, TTT tracking)
                           └─ CFlowX_Tags      (tag item text + color generation)
                                └─ CFlowX_Functions  (UI action handlers)
                                     └─ CFlowX       (main plugin, event dispatch)
```

`RadarScreen` (inherits `EuroScopePlugIn::CRadarScreen`) runs in parallel — handles visual rendering and radar-specific events independently from the main chain.

### Key data flow

1. EuroScope fires events → `CFlowX` dispatches to the appropriate layer
2. `OnGetTagItem()` → `CFlowX_Tags` methods → `tagInfo` (text + color)
3. `OnFunctionCall()` → `CFlowX_Functions` methods (HP assignment, freq transfer, etc.)
4. `OnTimer()` → `CFlowX_Timers` updates state maps (TTT, same-SID, QNH flags)
5. `OnNewMetarReceived()` → QNH change detection and orange "X" flagging
6. `OnFlightPlanDisconnect()` → cleanup aircraft from all state maps

### Configuration

`config.json` (project root, loaded at startup) defines all airport-specific data:
- Runways with threshold coordinates, holding point polygons, SID groups/colors
- Taxi-out stand polygons for push vs. taxi detection
- Ground/tower/approach frequencies and SID→frequency mappings
- NAP reminder and night SID settings

Structures are defined in `config.h`. Settings persistence (update check, flash, debug toggles) uses EuroScope's own settings storage via delimiter-separated strings.

### DIFLIS (Digital Flight Strip) window

Popout-capable electronic strip board, toggled from the Start menu → Windows → DIFLIS. Column/group layout is data-driven from `config.json` under each airport's `"diflis"` key (`col4WidthPercent` + `groups[]`). Group membership is derived from EuroScope state (ground status, clearance flag, airborne) with a DIFLIS-owned override map for states that have no EuroScope counterpart (e.g. `STRIP_STORAGE`, `STANDING_BY`). The strip cache is rebuilt every tick by `CFlowX_CustomTags::UpdateTagCache` into `RadarScreen::diflisStripsCache`; rendering is `RadarScreen::DrawDiflisWindow`. Data model lives in `DiflisModel.h`.

### Tag item columns

Tag items are identified by integer IDs in `constants.h`. Each column can display colored text; colors are also defined in `constants.h`. The departure list columns include: Push+Start helper, Taxi Out indicator, New QNH marker, Same SID tracker, Holding Point assignment, Takeoff Timer, TTT (time-to-takeoff for inbounds), and Tower sort key.

### Geometric detection

Point-in-polygon is used extensively for:
- Detecting which holding point an aircraft is at
- Detecting whether an aircraft is in a taxi-out stand (push vs. taxi)
- Any area-based state transitions

### Third-party plugin integration

- **TopSky**: clearance flag read/write, missed approach and highlighting
- **Ground Radar**: ground status reading/writing, auto stand assignment

## Testing / Debug commands

All commands prefixed with `.flowx` in the EuroScope chat:

| Command | Effect |
|---|---|
| `debug` | Toggle verbose debug logging |
| `nocheck` | Disable flight plan validations (offline testing) |
| `redoflags` | Re-evaluate clearance flags for all aircraft |
| `reset` | Reload configuration |
| `update` | Toggle update check |
| `flash` | Toggle message flashing |
| `gnd` / `twr` | Override detected ground/tower frequency |

## Code conventions

- Never use `goto` — use lambdas, `do { ... } while(false)`, or early returns instead.
- Do not create temporary PowerShell scripts in the project directory for file edits.
- Precompiled header is `pch.h` — include it first in every `.cpp`.
- Update documentation
- Don't auto commit — wait for explicit "commit" instruction; when committing, also push to remote.

### Class member ordering and alignment

Within every class, order members: `private` → `protected` → `public`.

Within each access section: variables first (grouped, no blank lines between them), then a blank line, then functions (with doc-comment blocks; blank lines between functions). Both groups sorted alphabetically.

Variable declarations use 3-column alignment (type, assignment, `///<` doc comment). **Do not manually align** — always run clang-format after adding or modifying members:

```
clang-format -i <file>
```

clang-format is on PATH. The `.clang-format` at the repo root configures `AlignConsecutiveDeclarations`, `AlignConsecutiveAssignments`, and `AlignTrailingComments` to produce the correct 3-column layout.
