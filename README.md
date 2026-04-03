# FlowX

FlowX is an [EuroScope](https://euroscope.hu/) plugin for VATSIM air traffic controllers. It is primarily aimed at delivery, ground, and tower controllers and provides departure management tooling, same-SID / wake-turbulence separation tracking, inbound time-to-touchdown display, automatic holding point detection, and a range of convenience functions.

The plugin ships with a `config.json` file that defines all airport-specific data. It is currently configured for **LOWW (Vienna International Airport)**. Adding other airports requires only changes to `config.json` — no recompilation is needed.

---

## Table of Contents

- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
- [Tag Items](#tag-items)
- [Tag Functions](#tag-functions)
- [Custom Windows](#custom-windows)
- [Chat Commands](#chat-commands)
- [config.json Reference](#configjson-reference)
- [Contributing](#contributing)
- [License](#license)

---

## Getting Started

### Prerequisites

- [EuroScope](https://euroscope.hu/) (developed against v3.2.9, confirmed working with v3.2.3 and later beta versions)
- A sector file / profile that includes the airports you want to configure in `config.json`

### Installation

1. Download the latest `FlowX.zip` from the [Releases](https://github.com/sushiat/FlowX/releases/latest) page.
2. Extract `FlowX.dll`, `config.json`, `nap.wav`, and `airbourne.wav` into your plugin directory.
3. In EuroScope open **OTHER SET → Plug-ins**, click **Load** and select `FlowX.dll`.
4. Successful load is confirmed in the **Messages** chat:
   ```
   [08:34:10] FlowX: Version 0.6.0 loaded.
   ```
5. Add the desired tag item columns to your departure list (see [Tag Items](#tag-items) below).

> **Sound files:** `nap.wav` plays when the NAP reminder window appears and stops on acknowledgement. `airbourne.wav` plays once when an aircraft is detected airborne. Both files must be placed alongside `FlowX.dll`.

> **`windowLocations.json`** is created automatically by the plugin in the same directory as `FlowX.dll`. It stores the screen positions of all five custom GDI windows and the last NAP reminder dismissal date. Delete it to reset all window positions to their defaults.

---

## Tag Items

Tag items are added to EuroScope departure lists or tag definitions via **Tag Item Type = FlowX / \<name\>**. Only the following five items are registered with EuroScope. All other columns (HP, spacing, TTT, etc.) are rendered inside the custom GDI windows described below.

| Tag Item | Typical display | Description |
|---|---|---|
| **Push+Start Helper** | `->121.600` / `OK` / `!RWY` / `...` | Validates the flight plan and shows the next frequency or an error code. Left/right click triggers the ONFREQ/ST-UP/PUSH function. See table below for all values. |
| **Taxi Out?** | `T` / `P` | Green `T` for taxi-out stands, `P` for pushback stands. Determined by point-in-polygon against `taxiOutStands` polygons in config. |
| **New QNH** | `X` (orange) | Appears when a METAR change contains a new QNH and the aircraft has a clearance. Click to acknowledge. |
| **Same SID** | `LANUX2A` | SID name colour-coded by group. Used in standard EuroScope departure lists. |
| **ADES Type-Y** | `LOWW` / `WW523` (turquoise) | Destination ICAO for standard IFR plans. For type-Y (mixed IFR/VFR) plans, shows the last IFR waypoint in turquoise instead. |

#### Push+Start Helper values

| Display | Colour | Meaning |
|---|---|---|
| `!RWY` | Red | No departure runway assigned |
| `!ASSR` | Red | No squawk code assigned |
| `!CLR` | Red | No clearance flag set |
| `1234` | Orange | Squawk not yet set by pilot |
| `OK` | Green | Cleared and ready; controller is GND or above |
| `->121.600` | Green | Cleared and ready; shows next frequency (GND, TWR, APP, CTR, or UNICOM) |

---

## Tag Functions

Only two tag functions are registered with EuroScope and can be assigned to tag column buttons. All other controller actions (HP assignment, line-up, takeoff, transfer, etc.) are triggered by left/right click inside the custom GDI windows.

| Function | Description |
|---|---|
| **Set ONFREQ/ST-UP/PUSH** | Sets the appropriate ground state. DEL position: sets ONFREQ. GND/TWR: detects push vs. taxi-out stand and sets ST-UP or ONFREQ accordingly. |
| **Clear New QNH** | Removes the new-QNH flag from the flight strip so the orange `X` disappears. |

---

## Custom Windows

FlowX draws five custom GDI windows on the radar screen. All windows are draggable by their title bar and persist their position between sessions via `windowLocations.json` in the plugin directory.

### DEP/H — Departure Rate

Shows the hourly departure count and 15-minute average spacing per runway. Rebuilds every second. No interaction.

### NAP Reminder

A modal overlay that appears at the configured local time (see `napReminder` in config) to alert controllers of the start of noise abatement procedures. `nap.wav` plays on appearance and stops on acknowledgement. Draggable; dismissed via the **ACK** button.

### TWR Outbound

Tracks all departing aircraft. Rows are sorted by a composite key (holding point position, then ground state, then callsign). Aircraft that have departed and are no longer being tracked are shown dimmed at reduced size.

| Column | Source | Description | Left click | Right click |
|---|---|---|---|---|
| C/S | EuroScope | Callsign | — | — |
| STS | FlowX | Ground state (`ONFREQ` / `START-UP` / `TAXI` / `LINE UP` / `TAKE OFF` / `T+M:SS`) | Line Up | Take Off |
| DEP? | FlowX | Departure readiness vs. previous departure: time (s) or distance (nm), colour-coded green/yellow/red | — | — |
| RWY | FlowX | Assigned departure runway | EuroScope runway selector | — |
| SID | FlowX | SID name colour-coded by group | EuroScope SID selector | — |
| WTC | EuroScope | Wake turbulence category | — | — |
| ATYP | TopSky | Aircraft type | — | — |
| Freq | FlowX | Next handoff frequency; turquoise above transfer altitude, blinking orange at warning threshold, `!MODE-C` if airborne without Mode-C | Transfer Next | — |
| HP | FlowX | Holding point; orange with `*` if readback pending, grey after departure | Assign HP | Request HP |
| Spacing | FlowX | Takeoff spacing — time (lighter follows heavier) or distance (equal/heavier follows lighter), colour-coded green/yellow/red | — | — |

#### Takeoff Spacing format

```
 90s /120    ← time (s) / required (s)   — lighter follows heavier
4.2nm/3      ← distance (nm) / required  — equal or heavier follows lighter
---          ← no preceding departure recorded
```

Required distance is 3 nm by default, 5 nm when both aircraft share a SID group.
Required time is derived from the holding point configuration (default 120 s).

### TWR Inbound

Tracks aircraft on approach, ordered by time to touchdown per runway. Aircraft furthest from the threshold are dimmed. Rows are grouped by runway with a blank separator between groups.

| Column | Source | Description | Left click | Right click |
|---|---|---|---|---|
| TTT | FlowX | Time to touchdown prefixed with runway (e.g. `29_03:42`); green >2 min, yellow >1 min, red <1 min. Go-arounds show the go-around frequency blinking red/yellow. | Start tracking | — |
| C/S | EuroScope | Callsign | Cleared to land | Missed approach |
| NM | FlowX | Leading inbound: absolute distance to threshold. Following inbounds: gap to aircraft ahead (`+2.3`), green >3 nm, yellow >2.5 nm, red <2.5 nm | — | — |
| SPD | EuroScope | Ground speed | — | — |
| WTC | EuroScope | Wake turbulence category | — | — |
| ATYP | TopSky | Aircraft type | — | — |
| Gate | Ground Radar | Assigned stand | Open stand menu | Auto stand assignment |
| Vacate | FlowX | Suggested vacate point based on assigned stand and gap to trailing inbound | — | — |
| RWY | FlowX | Assigned arrival runway; blinks red/yellow if tracked on a different runway than assigned | EuroScope runway selector | — |

### WX/ATIS

Shows wind, QNH, ATIS letter, and RVR (when present) for every airport in `config.json`. Wind/QNH/RVR are parsed from METAR; the ATIS letter is polled from the VATSIM v3 data feed every 60 seconds. Clicking an airport row acknowledges any pending QNH change.

| Column | Description |
|---|---|
| Wind | Direction/speed (`dddKKkt`); colour reflects conditions |
| QNH | Current QNH in hPa; turns orange when changed and unacknowledged |
| ATIS | Current ATIS letter; greyed out when no ATIS is online |
| RVR | RVR reading(s) from METAR, shown as a second line when present |

---

## Chat Commands

All commands are entered in any EuroScope chat channel, prefixed with `.flowx`.
Running `.flowx` alone prints the loaded version and a command list.

| Command | Saved | Description |
|---|---|---|
| `.flowx debug` | Yes | Toggle verbose debug logging in the Messages window |
| `.flowx update` | Yes | Toggle the background update check on startup |
| `.flowx flash` | Yes | Toggle flashing of unread message indicator for FlowX messages |
| `.flowx gnd` | No | Force ground station to be treated as online (cross-coupling scenarios) |
| `.flowx twr` | No | Force tower station to be treated as online (cross-coupling scenarios) |
| `.flowx redoflags` | — | Toggle all existing clearance flags off then back on (useful for newly joined controllers) |
| `.flowx autorestore` | Yes | Toggle auto-restore on quick reconnect. When enabled, if a pilot disconnects and reconnects within 90 seconds with a matching flight plan (same callsign, pilot name, airports, aircraft type, route, squawk, and position within 1 nm), their clearance flag and ground state are automatically restored. |
| `.flowx reset` | — | Reload `config.json` and reset all settings to defaults |
| `.flowx nocheck` | — | Disable flight-plan validation checks (offline testing only — do not use live) |

---

## config.json Reference

`config.json` lives alongside the DLL and is loaded at startup. It is a JSON object keyed by ICAO airport code. Multiple airports can be defined.

```json
{
    "LOWW": { ... },
    "LOWI": { ... }
}
```

### Top-level airport fields

| Field | Type | Description |
|---|---|---|
| `fieldElevation` | integer | Airport elevation in feet, used for airborne detection |
| `airborneTransfer` | integer | Altitude (ft) at which the TWR Next Freq tag turns turquoise |
| `airborneTransferWarning` | integer | Altitude (ft) at which it starts blinking orange |
| `gndFreq` | string | Default ground frequency |
| `defaultAppFreq` | string | Default approach frequency used when no SID-specific match exists |
| `ctrStations` | array of strings | Centre primary frequencies in priority order. The first frequency that has at least one online centre station wins. |

```json
"fieldElevation": 600,
"airborneTransfer": 1500,
"airborneTransferWarning": 3000,
"gndFreq": "121.6",
"defaultAppFreq": "134.675",
"ctrStations": ["129.200", "134.440", "134.350", "132.600"]
```

### `geoGndFreq`

Geographic ground frequency zones. When an aircraft's position falls inside a zone's polygon, that zone's frequency is used instead of `gndFreq`.

```json
"geoGndFreq": {
    "west": {
        "freq": "121.775",
        "lat": [48.123917, 48.113056, 48.117222, 48.129167],
        "lon": [16.533667, 16.567444, 16.570472, 16.536750]
    }
}
```

### `taxiOutStands`

Named polygons that identify taxi-out aprons. Aircraft inside these polygons receive ST-UP instead of ONFREQ when the ONFREQ/ST-UP function is triggered.

```json
"taxiOutStands": {
    "bstands": {
        "lat": [48.120075, 48.119410, 48.121754, 48.122101],
        "lon": [16.552188, 16.554506, 16.556262, 16.553642]
    }
}
```

### `napReminder`

Configures a once-per-session modal alert at a specific local time (e.g. to remind controllers of the start of noise abatement procedures).

```json
"napReminder": {
    "enabled": true,
    "hour": 20,
    "minute": 30,
    "tzone": "Europe/Vienna"
}
```

### `sidAppFreqs`

Maps approach frequencies to the list of SIDs that should be handed off to them. When a departing aircraft's SID matches an entry, that frequency is used for the handoff instead of `defaultAppFreq`.

```json
"sidAppFreqs": {
    "125.175": ["BUWUT2A", "LANUX4A", "LEDVA4A"],
    "129.050": ["ARSIN2A", "LUGEM2A", "MEDIX2A"]
}
```

### `appFreqFallbacks`

Maps each target approach frequency to a priority-ordered list of approach frequencies to try. When transferring a departure or displaying the next frequency, the plugin uses this list to find the best available station: for each frequency in the list it finds all online approach stations on that frequency, sorts their callsigns alphabetically, and picks the first. If no approach station is found across all fallbacks, centre stations are tried via `ctrStations`.

The target frequency itself should always be listed first.

```json
"appFreqFallbacks": {
    "134.675": ["134.675", "129.050", "118.775", "125.175"],
    "125.175": ["125.175", "129.050", "118.775", "134.675"]
}
```

### `nightTimeSids`

Night-time SIDs are filed with a truncated name (last character dropped), so `IRGOT2A` appears in the flight plan as `IRGO2A`. This map restores the full name for display: the key is the truncated SID prefix as filed, and the value is the full SID name prefix. The tag appends `*` to mark the SID as a night procedure.

```json
"nightTimeSids": {
    "IRGO": "IRGOT",
    "IMVO": "IMVOB"
}
```

For example, a filed SID of `IRGO2A` is displayed as `IRGOT2A*`.

### `runways`

Per-runway configuration keyed by runway designator.

| Field | Type | Description |
|---|---|---|
| `opposite` | string | Reciprocal runway designator (used for go-around detection) |
| `twrFreq` | string | Tower frequency for this runway |
| `goAroundFreq` | string | Go-around (approach) frequency for this runway |
| `width` | int | Runway width in metres (e.g. `45`) |
| `threshold` | object | `{ "lat": ..., "lon": ... }` — runway threshold coordinates |
| `sidGroups` | object | Group number → array of SID prefixes. Aircraft in the same group get 5 nm spacing instead of 3 nm. |
| `sidColors` | object | Colour name → array of SID prefixes. Valid colours: `green`, `orange`, `turq`, `purple`, `red`, `white`, `yellow`. SIDs omitted here default to white. |
| `holdingPoints` | object | Named holding point definitions (see below) |
| `vacatePoints` | object | Named vacate point definitions (see below) |

```json
"runways": {
    "11": {
        "opposite": "29",
        "twrFreq": "119.4",
        "goAroundFreq": "125.175",
        "threshold": { "lat": 48.122766, "lon": 16.533610 },
        "sidGroups": {
            "1": ["LANUX", "BUWUT", "LEDVA"],
            "2": ["OSPEN", "RUPET"]
        },
        "sidColors": {
            "green":  ["LANUX", "BUWUT", "LEDVA"],
            "orange": ["OSPEN", "RUPET"]
        },
        ...
    }
}
```

#### Holding points

Each holding point entry defines the polygon that detects when an aircraft is at that point.

| Field | Type | Description |
|---|---|---|
| `assignable` | boolean | If `true`, the point appears in the HP popup list |
| `sameAs` | string | Name of another point considered physically equivalent for spacing calculations |
| `polygon` | object | `{ "lat": [...], "lon": [...] }` — detection polygon vertices |

```json
"holdingPoints": {
    "A12": {
        "assignable": true,
        "sameAs": "A11",
        "polygon": {
            "lat": [48.1231, 48.1228, 48.1239, 48.1241],
            "lon": [16.5335, 16.5344, 16.5347, 16.5342]
        }
    },
    "A9": {
        "assignable": true,
        "polygon": { ... }
    }
}
```

#### Vacate points

Vacate points define recommended runway exit points for arriving aircraft based on their assigned stand and the gap to the following (trailing) inbound.

| Field | Type | Description |
|---|---|---|
| `minGap` | number | Minimum gap in NM to the following (trailing) inbound required before this vacate is suggested |
| `stands` | array of strings | Stand names (or glob-style patterns with `*`) associated with this vacate |

```json
"vacatePoints": {
    "A3": {
        "minGap": 5,
        "stands": ["F04", "F08", "H*", "K*"]
    },
    "A4": {
        "minGap": 3,
        "stands": ["E*", "F*"]
    }
}
```

---

## Contributing

If you have a suggestion or encountered a bug, please open an [issue](https://github.com/sushiat/FlowX/issues) on GitHub. Include a description of the problem, relevant logs, and steps to reproduce.

[Pull requests](https://github.com/sushiat/FlowX/pulls) are welcome. Please keep features reasonably generic — the plugin is intended to be configurable via `config.json` rather than hard-coded for a specific airport or vACC.

### Development setup

- **Visual Studio 2022** (no other build system is supported)
- Set the environment variable `EUROSCOPE_ROOT` to the EuroScope install directory (not the executable itself) to enable the debugger launch configuration
- Avoid breakpoints during live controlling — use `.flowx debug` instead
- Target: 32-bit or 64-bit DLL (`Release|Win32` or `Release|x64`), C++17, Windows SDK 11.0

Dependencies are bundled in `include/` and `lib/`:

| Library | Version | License | Purpose |
|---|---|---|---|
| EuroScope SDK | — | — | Plugin base classes |
| [nlohmann/json](https://github.com/nlohmann/json/) | v3.9.1 | MIT | JSON config parsing |
| [semver](https://github.com/Neargye/semver) | v0.2.2 | MIT | Version comparison for update check |
| [date/tz](https://github.com/HowardHinnant/date) | — | MIT | IANA timezone support for NAP reminder |

---

## License

[MIT License](LICENSE)
