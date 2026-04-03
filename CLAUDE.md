# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DelHelX is a EuroScope plugin (Windows DLL) for VATSIM air traffic controllers. It assists delivery/ground/tower controllers with departure management, holding point assignments, and same-SID tracking. Currently configured primarily for LOWW (Vienna).

## Build

Open `DelHelX.sln` in **Visual Studio 2022** and build with MSBuild. No CLI build commands are available.

- Output: 32-bit or 64-bit DLL (`Release|Win32` or `Release|x64`)
- Language standard: C++17
- EuroScope lib: `lib\EuroScope\EuroScopePlugInDll.lib`
- External headers: `include/` (nlohmann/json, semver, date/tz)

**Debug setup:** Set `EUROSCOPE_ROOT` environment variable to the EuroScope install directory. The debugger launches EuroScope.exe automatically. Avoid breakpoints during live sessions — use `.delhelx debug` instead.

## Architecture

The plugin uses a deep inheritance chain (bottom to top):

```
EuroScopePlugIn::CPlugIn
  └─ CDelHelX_Base       (radar screen creation and plugin registration calls)
       └─ CDelHelX_Logging    (chat message logging)
            └─ CDelHelX_Settings  (config.json + EuroScope settings)
                 └─ CDelHelX_LookupsTools  (geometry: point-in-polygon, distances)
                      └─ CDelHelX_Timers   (aircraft state maps, TTT tracking)
                           └─ CDelHelX_Tags      (tag item text + color generation)
                                └─ CDelHelX_Functions  (UI action handlers)
                                     └─ CDelHelX       (main plugin, event dispatch)
```

`RadarScreen` (inherits `EuroScopePlugIn::CRadarScreen`) runs in parallel — handles visual rendering and radar-specific events independently from the main chain.

### Key data flow

1. EuroScope fires events → `CDelHelX` dispatches to the appropriate layer
2. `OnGetTagItem()` → `CDelHelX_Tags` methods → `tagInfo` (text + color)
3. `OnFunctionCall()` → `CDelHelX_Functions` methods (HP assignment, freq transfer, etc.)
4. `OnTimer()` → `CDelHelX_Timers` updates state maps (TTT, same-SID, QNH flags)
5. `OnNewMetarReceived()` → QNH change detection and orange "X" flagging
6. `OnFlightPlanDisconnect()` → cleanup aircraft from all state maps

### Configuration

`config.json` (project root, loaded at startup) defines all airport-specific data:
- Runways with threshold coordinates, holding point polygons, SID groups/colors
- Taxi-out stand polygons for push vs. taxi detection
- Ground/tower/approach frequencies and SID→frequency mappings
- NAP reminder and night SID settings

Structures are defined in `config.h`. Settings persistence (update check, flash, debug toggles) uses EuroScope's own settings storage via delimiter-separated strings.

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

All commands prefixed with `.delhelx` in the EuroScope chat:

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

Variable declarations use **3-column alignment** (columns computed per access section):
- **Col 1 — Definition** (`type name`): right-padded to the length of the longest `type name` in the section.
- **Col 2 — Assignment** (`= value;` or `;`): starts one column past Col 1. Variables with an initializer get `= value;` here; variables without one get `;` immediately after their name (no pre-padding), then fill with spaces to Col 3.
- **Col 3 — Documentation** (`///<`): starts 2 columns past the end of the longest full declaration (`type name = value;`) in the section.

```cpp
// Example — longest definition is `airports` (43 chars), longest assignment is `= false;` (8 chars):
    std::map<std::string, airport> airports;          ///< Airport configurations keyed by ICAO code
    bool autoRestore                        = false;  ///< Whether auto-restore is enabled
    int depRateWindowX                      = -1;     ///< Last-saved X position; -1 = not yet positioned
    std::future<std::string> latestVersion;           ///< Async future for version fetch
    bool updateCheck;                                 ///< Whether update check is enabled
```
