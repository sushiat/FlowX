# DelHelX

DelHelX is an [EuroScope](https://euroscope.hu/) plugin for VATSIM air traffic controllers. It is primarily aimed at delivery, ground, and tower controllers and provides departure management tooling, same-SID / wake-turbulence separation tracking, inbound time-to-touchdown display, automatic holding point detection, and a range of convenience functions.

The plugin ships with a `config.json` file that defines all airport-specific data. It is currently configured for **LOWW (Vienna International Airport)**. Adding other airports requires only changes to `config.json` — no recompilation is needed.

---

## Table of Contents

- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
- [Tag Items](#tag-items)
- [Tag Functions](#tag-functions)
- [Flight Plan Lists](#flight-plan-lists)
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

1. Download the latest `DelHelX.zip` from the [Releases](https://github.com/sushiat/DelHelX/releases/latest) page.
2. Extract `DelHelX.dll` and `config.json` into your plugin directory.
3. In EuroScope open **OTHER SET → Plug-ins**, click **Load** and select `DelHelX.dll`.
4. Successful load is confirmed in the **Messages** chat:
   ```
   [08:34:10] DelHelX: Version 0.6.0 loaded.
   ```
5. Add the desired tag item columns to your departure list (see [Tag Items](#tag-items) below).

---

## Tag Items

Tag items are added to EuroScope departure lists or tag definitions via **Tag Item Type = DelHelX / \<name\>**.

### Delivery / Ground tags

| Tag Item | Typical display | Description |
|---|---|---|
| **Push+Start Helper** | `->121.600` / `OK` / `!RWY` / `...` | Validates the flight plan and shows the next frequency or an error code. Left/right click triggers the ONFREQ/ST-UP/PUSH function. See table below for all values. |
| **Taxi Out?** | `T` / `P` | Green `T` for taxi-out stands, `P` for pushback stands. Determined by point-in-polygon against `taxiOutStands` polygons in config. |
| **New QNH** | `X` (orange) | Appears when a METAR change contains a new QNH and the aircraft already has a clearance. Click to acknowledge. |
| **GND State Expanded** | `TAXI` / `LINE UP` / `TAKE OFF` / `...` | Human-readable expansion of the EuroScope ground state. See table below for all states. |
| **Departure Info** | `OK` / `45s` / `2.3nm` | Departure readiness relative to the previous departure from the same runway. Shows time remaining (seconds) or distance gap (nm) until separation is met. Green = OK or close, yellow = almost, red = not yet. |

#### Push+Start Helper states

The following table details all possible states shown by the **Push+Start Helper** tag above. Each state maps to a display string and colour; the table below describes what each means.

| Display | Colour | Meaning |
|---|---|---|
| `!RWY` | Red | No departure runway assigned |
| `!ASSR` | Red | No squawk code assigned |
| `!CLR` | Red | No clearance flag set |
| `1234 !CLR` | Orange | Squawk not set by pilot, plus the clearance error above |
| `OK` | Green | Cleared and ready; current position is GND or above |
| `->121.600` | Green | Cleared and ready; shows next frequency (GND, TWR, APP, CTR, or UNICOM) |
| `->121.600` | Yellow/White blinking | Aircraft is near the holding point or runway threshold |

#### GND State Expanded states

| Display | Colour | Meaning |
|---|---|---|
| `ONFREQ` | Default | Aircraft is on frequency |
| `START-UP` | Default | Startup/pushback clearance given (ST-UP) |
| `TAXI` | Default | Aircraft taxiing |
| `LINE UP` | Turquoise | Lined up on runway (LINEUP) |
| `TAKE OFF` | Green | Departed (DEPA) |
| `--DEP--` | Default | Aircraft has taken off and is being tracked as a departure |

### Tower departure tags

| Tag Item | Typical display | Description |
|---|---|---|
| **Departure Info** | `OK` / `45s` / `2.3nm` | Departure readiness relative to the previous departure from the same runway. Also listed under Delivery/Ground tags — applies once the aircraft is in TAXI or DEPA state. |
| **Assigned Runway** | `11` | Departure runway from the flight plan. |
| **HP1 / HP2 / HP3** | `A12` / `A12*` | Holding point assigned to slot 1/2/3. Starred (`*`) if it is a request awaiting readback. Orange if the assignment was set but not yet confirmed. |
| **HP Other** | `A9` | Freely assigned holding point from a popup list. |
| **Takeoff Spacing** | ` 90  [120]` / `4.2 nm [3]` | Shows time or distance separation from the previous departure. **Time** (lighter follows heavier) or **distance** (equal/heavier follows lighter). Colour-coded green/yellow/red against the required value. |
| **TWR Sort Key** | *(internal sort string)* | Internal key used to sort the TWR Outbound list — not intended to be human-readable. Only used for list ordering; suggest setting column width to 1. |
| **TWR Next Freq** | `->123.800` | Next handoff frequency for a departing aircraft (approach, centre, or UNICOM). Turns turquoise at the transfer altitude and blinks orange at the transfer altitude warning threshold. Displays `!MODE-C` (blinking orange/red) when the aircraft is airborne but the transponder isn't in Mode-C. |

#### Takeoff Spacing format

```
 90  s  /120    ← time (s) / required (s)   — lighter follows heavier
4.2 nm /3       ← distance (nm) / required  — equal or heavier follows lighter
---             ← no preceding departure recorded
```

Required distance is 3 nm by default, 5 nm when both aircraft are in the same SID group.
Required time is computed from holding point configuration (default 120 s).

### Inbound tags

| Tag Item | Typical display | Description |
|---|---|---|
| **TTT** | `29_03:42` / `29_-042` | Time to touchdown based on distance and groundspeed, prefixed with the arrival runway designator (e.g. `29_03:42`). Colour-coded green (>2 min) / yellow (>1 min) / red (<1 min). Go-arounds show a negative elapsed counter (e.g. `29_-042`) blinking red/yellow. |
| **Inbound NM** | `8.4` / `+2.3` | Two display modes. The **leading** inbound on each runway shows its absolute distance to the threshold (e.g. `8.4`). All **following** aircraft show the gap to the aircraft ahead prefixed with `+` (e.g. `+2.3`), colour-coded green (>3 nm) / yellow (>2.5 nm) / red (<2.5 nm). |
| **Assigned Arrival RWY** | `29` | Arrival runway from the flight plan. Blinks red/yellow if the aircraft is tracked in the TTT list but approaching a different runway than assigned. |
| **Suggested Vacate** | `A4` | Recommended vacate point based on the aircraft's assigned stand and the gap to the following (trailing) inbound, from `vacatePoints` in config. |

---

## Tag Functions

Tag functions are assigned to the **Left button** or **Right button** action of a tag column.

| Function | Description |
|---|---|
| **Set ONFREQ/ST-UP/PUSH** | Sets the appropriate ground state. DEL position: sets ONFREQ. GND/TWR: detects push vs. taxi-out stand and sets ST-UP or ONFREQ accordingly. |
| **Clear New QNH** | Removes the new-QNH flag from the flight strip so the orange `X` disappears. |
| **Assign HP1 / HP2 / HP3** | Writes the runway's default holding point for that slot into flight-strip annotation slot 8 and syncs to other controllers. |
| **Request HP1 / HP2 / HP3** | Same as Assign, but appends `*` to indicate a readback is pending. |
| **Assign Other HP** | Opens a popup list of all `assignable` holding points. Selection writes the choice to slot 8. |
| **Request Other HP** | Same as Assign Other HP, but appends `*`. |
| **Line Up** | Sets the LINEUP ground state. |
| **Take Off** | Sets the DEPA ground state and starts departure tracking. |
| **Transfer Next** | Hands the aircraft off to approach, centre, or drops tracking. Picks the SID-specific approach frequency when configured. |
| **Cleared to Land** | Drops tracking and triggers the TopSky Strong-Highlight cleared-to-land indicator. |
| **Missed Approach** | Starts tracking, assigns 5000 ft, and triggers the TopSky missed-approach highlight. |
| **Auto Stand Assignment** | Triggers automatic stand assignment via the Ground Radar plugin. |

---

## Flight Plan Lists

DelHelX registers two built-in flight plan lists that appear automatically on first load.

### TWR Outbound

Tracks all departing aircraft from takeoff until they leave the controlled area. Columns:

| Column | Source | Description | Left click | Right click |
|---|---|---|---|---|
| C/S | EuroScope | Callsign | — | — |
| STS | DelHelX | Ground state (GND State Expanded) | Line Up | Take Off |
| DEP? | DelHelX | Departure readiness relative to previous departure (Departure Info) | — | — |
| RWY | DelHelX | Assigned departure runway | — | — |
| SID | DelHelX | SID name colour-coded by group (Same SID tracker) | — | — |
| WTC | EuroScope | Wake turbulence category | — | — |
| ATYP | TopSky | Aircraft type | — | — |
| Freq | DelHelX | Next handoff frequency (TWR Next Freq) | Transfer Next | — |
| HP1–HP3 | DelHelX | Holding point slots | Assign HP | Request HP (pending readback) |
| HPO | DelHelX | Other holding point | Assign Other HP | Request Other HP |
| Spacing | DelHelX | Takeoff spacing (time or distance) | — | — |
| S | DelHelX | TWR sort key (internal, width 1) | — | — |

### TWR Inbound

Tracks aircraft on approach ordered by distance to the runway threshold. Columns:

| Column | Source | Description | Left click | Right click |
|---|---|---|---|---|
| TTT | DelHelX | Time to touchdown, prefixed with runway designator (e.g. `29_03:42`) | Start tracking | — |
| C/S | EuroScope | Callsign | Cleared to land | Missed App |
| NM | DelHelX | Distance to threshold or gap to leading inbound | — | — |
| SPD | EuroScope | Ground speed | — | — |
| WTC | EuroScope | Wake turbulence category | — | — |
| ATYP | TopSky | Aircraft type | — | — |
| Gate | Ground Radar | Assigned stand | Open stand menu | Auto stand assignment |
| Vacate | DelHelX | Suggested vacate point | — | — |
| RWY | DelHelX | Assigned arrival runway (Assigned Arrival RWY). Blinks red/yellow if the aircraft is tracked in TTT but approaching a different runway than assigned. | Assign runway | — |

---

## Chat Commands

All commands are entered in any EuroScope chat channel, prefixed with `.delhelx`.
Running `.delhelx` alone prints the loaded version and a command list.

| Command | Saved | Description |
|---|---|---|
| `.delhelx debug` | Yes | Toggle verbose debug logging in the Messages window |
| `.delhelx update` | Yes | Toggle the background update check on startup |
| `.delhelx flash` | Yes | Toggle flashing of unread message indicator for DelHelX messages |
| `.delhelx gnd` | No | Force ground station to be treated as online (cross-coupling scenarios) |
| `.delhelx twr` | No | Force tower station to be treated as online (cross-coupling scenarios) |
| `.delhelx redoflags` | — | Toggle all existing clearance flags off then back on (useful for newly joined controllers) |
| `.delhelx autorestore` | Yes | Toggle auto-restore on quick reconnect. When enabled, if a pilot disconnects and reconnects within 90 seconds with a matching flight plan (same callsign, pilot name, airports, aircraft type, route, squawk, and position within 1 nm), their clearance flag and ground state are automatically restored. |
| `.delhelx reset` | — | Reload `config.json` and reset all settings to defaults |
| `.delhelx nocheck` | — | Disable flight-plan validation checks (offline testing only — do not use live) |

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
| `ctrStations` | array of strings | Callsign prefixes considered as centre stations (e.g. `"LOVV_CTR"`) |

```json
"fieldElevation": 600,
"airborneTransfer": 1500,
"airborneTransferWarning": 3000,
"gndFreq": "121.6",
"defaultAppFreq": "134.675",
"ctrStations": ["LOVV_E", "LOVV_N", "LOVV_CTR"]
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

### `appFreqStations`

Maps each approach frequency to a priority-ordered list of station callsign prefixes. When transferring a departure, the plugin iterates this list for the target frequency and hands off to the first online station. If no approach station is found, centre stations are tried in the order defined by `ctrStations`.

```json
"appFreqStations": {
    "134.675": ["LOWW_APP", "LOWW_P_APP", "LOWW_N_APP", "LOWW_M_APP"],
    "125.175": ["LOWW_M_APP", "LOWW_P_APP", "LOWW_N_APP", "LOWW_APP"]
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
| `index` | integer | Slot index: 1 = HP1, 2 = HP2, 3 = HP3, 4 = HPO |
| `assignable` | boolean | If `true`, the point appears in the HP Other popup list |
| `sameAs` | string | Name of another point considered physically equivalent for spacing calculations |
| `polygon` | object | `{ "lat": [...], "lon": [...] }` — detection polygon vertices |

```json
"holdingPoints": {
    "A12": {
        "index": 1,
        "sameAs": "A11",
        "polygon": {
            "lat": [48.1231, 48.1228, 48.1239, 48.1241],
            "lon": [16.5335, 16.5344, 16.5347, 16.5342]
        }
    },
    "A9": {
        "index": 4,
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

If you have a suggestion or encountered a bug, please open an [issue](https://github.com/sushiat/DelHelX/issues) on GitHub. Include a description of the problem, relevant logs, and steps to reproduce.

[Pull requests](https://github.com/sushiat/DelHelX/pulls) are welcome. Please keep features reasonably generic — the plugin is intended to be configurable via `config.json` rather than hard-coded for a specific airport or vACC.

### Development setup

- **Visual Studio 2022** (no other build system is supported)
- Set the environment variable `EUROSCOPE_ROOT` to the EuroScope install directory (not the executable itself) to enable the debugger launch configuration
- Avoid breakpoints during live controlling — use `.delhelx debug` instead
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
