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
- [Start Menu](#start-menu)
- [config.json Reference](#configjson-reference)
- [Testing](#testing)
- [Contributing](#contributing)
- [License](#license)

---

## Getting Started

### Prerequisites

- [EuroScope](https://euroscope.hu/) (developed against v3.2.9, confirmed working with v3.2.3 and later beta versions)
- A sector file / profile that includes the airports you want to configure in `config.json`

### Installation

1. Download the latest `FlowX.zip` from the [Releases](https://github.com/sushiat/FlowX/releases/latest) page.
2. Extract `FlowX.dll`, `config.json`, `nap.wav`, `airbourne.wav`, `readyTakeoff.wav`, `gndtransfer.wav`, `click.wav`, `noRoute.wav`, and `taxiConflict.wav` into your plugin directory.
3. In EuroScope open **OTHER SET → Plug-ins**, click **Load** and select `FlowX.dll`.
4. Successful load is confirmed in the **Messages** chat:
   ```
   [08:34:10] FlowX: Version 0.6.0 loaded.
   ```
5. Add the desired tag item columns to your departure list (see [Tag Items](#tag-items) below).

> **Sound files** (all must be placed alongside `FlowX.dll`):
> - `nap.wav` — plays when the NAP reminder window appears; stops on acknowledgement.
> - `airbourne.wav` — plays once when an aircraft is detected airborne.
> - `readyTakeoff.wav` — plays when a lined-up aircraft has been clear for takeoff for 5 seconds (departure separation resolved).
> - `gndtransfer.wav` — plays when a landed inbound transitions to GND frequency.
> - `click.wav` — plays on start-menu clicks.
> - `noRoute.wav` — plays when the taxi router cannot find a valid route to the assigned holding point.
> - `taxiConflict.wav` — plays when a taxi conflict is detected between two aircraft with active routes.

> **`settings.json`** is created automatically by the plugin in the same directory as `FlowX.dll`. It stores all plugin preferences, the screen positions of all custom windows, and the last NAP reminder dismissal date. Delete it to reset everything to defaults.

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

FlowX draws six custom GDI windows on the radar screen. All windows are draggable by their title bar and persist their position between sessions via `settings.json` in the plugin directory.

### Approach Estimate

A vertical time bar showing all tracked inbound aircraft for up to two runway groups (left / right), ordered by estimated time to touchdown. Each runway's aircraft appear on the side configured via `estimateBarSide` in `config.json`. Aircraft labels are coloured by inbound-list colour when **Appr Est Colors** is enabled, or always green otherwise. Go-around aircraft are shown with a red background; TTT-frozen aircraft with yellow. The window is resizable by dragging its lower-right corner.

### DEP/H — Departure Rate

Shows the hourly departure count and 15-minute average spacing per runway. Rebuilds every second. No interaction.

### NAP Reminder

A modal overlay that appears at the configured local time (see `napReminder` in config) to alert controllers of the start of noise abatement procedures. `nap.wav` plays on appearance and stops on acknowledgement. Draggable; dismissed via the **ACK** button.

### TWR Outbound

Tracks all departing aircraft. Rows are sorted by a composite key (holding point position, then ground state, then callsign). Aircraft that have departed and are no longer being tracked are shown dimmed at reduced size.

| Column | Source | Description | Left click | Right click |
|---|---|---|---|---|
| C/S | EuroScope | Callsign | — | — |
| STS | FlowX | Ground state (`ONFREQ` / `START-UP` / `TAXI` / `LINE UP` / `TAKE OFF` / `--DEP--`) | Line Up | Take Off |
| DEP? | FlowX | Departure readiness vs. previous departure: time (s) or distance (nm), colour-coded green/yellow/red | — | — |
| RWY | FlowX | Assigned departure runway | EuroScope runway selector | — |
| SID | FlowX | SID name colour-coded by group | EuroScope SID selector | — |
| WTC | EuroScope | Wake turbulence category | — | — |
| ATYP | TopSky | Aircraft type | — | — |
| Freq | FlowX | Next handoff frequency; turquoise above transfer altitude, blinking orange at warning threshold, `!MODE-C` if airborne without Mode-C | Transfer Next | — |
| HP | FlowX | Holding point; orange with `*` if readback pending, grey after departure | Assign HP | Request HP |
| # | FlowX | Departure queue position (1-based); empty if not yet queued | — | — |
| Spacing | FlowX | Takeoff spacing snapshot — time (lighter follows heavier) or distance (equal/heavier follows lighter), colour-coded green/yellow/red | — | — |
| T+ | FlowX | Elapsed time since takeoff roll start (`M:SS`); empty while on the ground | — | — |
| dNM | FlowX | Live distance to the previous departure (`XX.X nm`), updated on every position report; colour-coded green/yellow/red using the same distance thresholds as Spacing; `---` if no previous departure tracked | — | — |

#### Takeoff Spacing / dNM format

```
 90s /120    ← Spacing: time (s) / required (s)   — lighter follows heavier
4.2nm/3      ← Spacing: distance (nm) / required  — equal or heavier follows lighter
---          ← no preceding departure recorded

12.3 nm      ← dNM: live current distance to previous departure
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

## Start Menu

Click the **FlowX** button in the bottom-right corner of the radar screen to open the start menu. It has three sections:

**Windows** — toggle visibility of each custom window:
Approach Estimate, DEP/H, TWR Outbound, TWR Inbound, WX/ATIS.

**Commands** — one-shot actions:

| Item | Description |
|---|---|
| Redo CLR flags | Toggles all existing clearance flags off then back on (same as `.flowx redoflags`) |
| Dismiss QNH | Bulk-clears all pending QNH change markers |
| Save positions | Saves current window positions to `settings.json` |

**Options** — persistent toggles (saved to `settings.json`):

| Option | Default | Description |
|---|---|---|
| Debug mode | Off | Verbose debug logging in the Messages window |
| Auto-Restore FPLN | Off | Auto-restores clearance flag and ground state on quick reconnect (within 90 s) |
| Update check | On | Background check for a newer plugin version on startup |
| Flash messages | Off | Flashes the unread message indicator for FlowX messages |
| Auto Parked | On | Automatically sets arriving aircraft to PARK when they stop at their assigned stand |
| Appr Est Colors | Off | Uses inbound-list colours in the Approach Estimate window instead of always-green |
| Auto-Clear Scratch | Off | Automatically clears the scratchpad when this controller clicks LINEUP or DEPA, unless the content starts with a prefix in `scratchpadClearExclusions` |
| HP auto-scratch | On | When a GND controller sets a scratchpad entry beginning with `.`, automatically assigns the matching holding point and confirms it via scratchpad |
| Show TAXI routes | Off | Show all active taxi routes and push zone reservations on the radar screen (individual routes are always shown on hover regardless of this setting) |
| Fonts | — | Increase / decrease font size offset for all custom windows |
| BG opacity | 100% | Increase / decrease background opacity of all custom windows (20–100%) |

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

### `taxiOnlyZones`

Named polygons that always force taxi planning mode, regardless of stand assignment or clearance state. Use for remote aprons or cargo areas that never require a push-back tug.

Same polygon format as `taxiOutStands`.

```json
"taxiOnlyZones": {
    "GAC": {
        "lat": [48.124791, 48.126092, 48.129959, 48.128719],
        "lon": [16.537689, 16.533779, 16.536438, 16.540431]
    }
}
```

### Taxi intersection classification

All `aeroway=taxiway` and `aeroway=taxilane` ways from OSM are included in the taxi graph automatically. The `taxiIntersections` array reclassifies specific ways as intersection type, which affects routing penalties and graph behaviour. Entries ending with `*` are treated as prefix patterns.

| Field | Type | Description |
|---|---|---|
| `taxiIntersections` | array of strings | Intersection/exit refs or prefix patterns to reclassify (e.g. `"Exit *"`, `"Exit 12"`) |

```json
"taxiIntersections": ["Exit *"]
```

### `taxiWingspanMax`

Maps taxiway or taxilane refs to a maximum wingspan in metres. Aircraft **wider** than the limit are hard-blocked — the router never uses that element for them.

```json
"taxiWingspanMax": {
    "P":  36.0,
    "TL 40 \"Blue Line\"": 36.0
}
```

### `taxiWingspanAvoid`

Maps taxiway or taxilane refs to a maximum wingspan in metres. Aircraft **at or below** the limit receive a soft cost penalty on that ref, steering the router toward a parallel narrower lane when one is available. Unlike `taxiWingspanMax` this is not a hard block — the router can still use the ref if no alternative exists (e.g. an initial shared segment).

The penalty multiplier is set via `taxiNetworkConfig.edgeCosts.multWingspanAvoid` (default `3.0`).

```json
"taxiWingspanAvoid": {
    "TL 40": 36.0
}
```

This example steers aircraft with wingspan ≤ 36 m away from the centre TL40 lane toward `TL 40 "Blue Line"` / `TL 40 "Orange Line"`. Wide-body aircraft (> 36 m) are already hard-excluded from Blue/Orange Line by `taxiWingspanMax` and continue to use the centre lane normally.

### `taxiLaneSwingoverPairs`

Pairs of taxilane refs that are physically the same strip painted with two direction-of-travel markings. The taxi router treats them as freely interchangeable — an aircraft assigned to either lane may use the other without penalty.

```json
"taxiLaneSwingoverPairs": [
    ["TL 40 \"Blue Line\"", "TL 40 \"Orange Line\""]
]
```

### `taxiFlowGeneric`

Taxiway direction rules that are always active, regardless of which runways are in use. Each rule specifies a preferred direction of travel on a named taxiway. The router applies a cost penalty to edges that go against the grain.

```json
"taxiFlowGeneric": [
    { "taxiway": "P", "direction": "N" },
    { "taxiway": "Q", "direction": "S" }
]
```

### `taxiFlowConfigs`

Per-runway-configuration taxiway direction rules. The map key identifies the active runway configuration as `"<dep>_<arr>"`, where multiple runways on one side are joined with `/`. Rules in the matching entry are applied on top of `taxiFlowGeneric`.

Key examples:

| Config | Key |
|---|---|
| DEP 29, ARR 29 | `"29_29"` |
| DEP 16, ARR 11 | `"16_11"` |
| DEP 29 + 16, ARR 16 (split dep) | `"29/16_16"` |
| DEP 16, ARR 11 + 16 (sim landings) | `"16_11/16"` |

```json
"taxiFlowConfigs": {
    "29_29": [
        { "taxiway": "M", "direction": "E" },
        { "taxiway": "L", "direction": "W" },
        { "taxiway": "W", "direction": "S" }
    ],
    "16_11": [],
    "29/16_16": []
}
```

Entries with an empty array are valid and simply apply no additional rules beyond `taxiFlowGeneric`. Configurations not present in the map inherit only `taxiFlowGeneric`.

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

### `scratchpadClearExclusions`

A list of scratchpad prefixes that are exempt from the **Auto-Clear Scratch** feature. Comparison is case-insensitive. If the aircraft's scratchpad starts with any listed prefix the auto-clear is skipped.

```json
"scratchpadClearExclusions": [".cs", ".new"]
```

### `standRoutingTargets`

Maps stand names to the label of an OSM holding position node. When an inbound aircraft is assigned one of these stands, the taxi router terminates at that holding position instead of routing to the centre of the stand polygon. Use this for uncontrolled aprons where the tower hands off to a marshaller at a defined point.

The label must match the `ref` tag of the OSM `aeroway=holding_position` node exactly.

```json
"standRoutingTargets": {
    "GAC": "P1"
}
```

If the label is not found in the OSM graph data, routing falls back to the stand centroid.

### `preferredRoutes`

Targeted routing overrides for destinations whose real-world standard routing can't be reproduced by the global edge weights, flow rules, and turn penalties alone. Rules are grouped per runway configuration (same normalized `"<dep>_<arr>"` keying as `taxiFlowConfigs`) and evaluated at route planning time: the destination name — a stand designator for inbounds, a holding-point label for outbounds — is matched against each rule's `destination` regex, and the **first** rule whose destination matches and whose optional origin filters are satisfied wins. The router then produces a route whose ordered wayref list contains the rule's `mustInclude` sequence as an in-order subsequence (other wayrefs may appear between the required entries). If no such route is feasible from the aircraft's current position, routing falls back to the unconstrained result and a `[PREF]` line is logged to the debug log — rules degrade gracefully rather than failing hard.

| Field | Type | Description |
|---|---|---|
| `destination` | string (ECMAScript regex) | Matched as a full match against the bare destination name (no `ICAO:` prefix, no `HP:` prefix). Stands and holding points share the same namespace; disambiguate with the pattern itself. |
| `origin` | string (ECMAScript regex, optional) | Allow-list on the aircraft's starting wayref. When set, the rule only fires if the origin wayref is **known** (non-empty) **and** matches this pattern. Origin wayref resolves by stand-polygon containment first (the aircraft's parked stand name) and falls back to the nearest graph node's wayref (the taxiway it's currently rolling on). |
| `originExclude` | string (ECMAScript regex, optional) | Deny-list on the starting wayref. When set, the rule is skipped if the origin wayref is known and matches this pattern. An unknown origin bypasses the deny-list — rules scoped purely by exclusion still fire when the aircraft's starting wayref can't be resolved. |
| `mustInclude` | array of strings | Ordered wayref sequence (same strings that appear in `TaxiRoute::wayRefs` debug output, e.g. `"W"`, `"Exit 22"`, `"TL 40 \"Blue Line\""`). The sequence must appear in order in the chosen route; unrelated wayrefs may appear between consecutive required entries. |

Regex ranges with parity are expressed with character classes — no custom syntax is needed. For example, to match every even stand in F04–F36:

```
F(0[2468]|[123][02468])
```

Stand-range origin filters work the same way — `"E4[1-7]"` matches an aircraft parked on any of E41 through E47, and `"TL 4[0-9]"` excludes any aircraft currently rolling on TL 40–49.

Example:

```json
"preferredRoutes": {
    "16_11": [
        {
            "destination":   "B1",
            "originExclude": "TL 4[0-9]",
            "mustInclude":   ["W", "Exit 21", "Exit 31"]
        },
        {
            "destination":   "F(0[2468]|[123][02468])",
            "origin":        "Exit 2[0-9]|W",
            "mustInclude":   ["W", "Exit 22", "Exit 32"]
        }
    ],
    "29_34": [
        { "destination": "A1", "mustInclude": ["M", "Exit 2", "L", "W"] }
    ]
}
```

Because rules are hard constraints with graceful fallback, keep the list small and reserve it for cases where global tuning can't converge. The first-match-wins order means more specific rules should come before more general ones, and origin filters should be used to scope rules away from aircraft that don't need them.

### `taxiNetworkConfig`

Optional fine-tuning of the taxi graph builder, A\* router, interactive snapping, and safety monitor. Every sub-section and every field is optional — omitting any of them leaves the corresponding parameter at its default. All defaults match the values previously hardcoded in the plugin, so existing airports need no changes to `config.json`.

#### `graph` — graph construction

| Field | Type | Default | Description |
|---|---|---|---|
| `subdivisionIntervalM` | number | `15.0` | Long OSM way segments are subdivided into waypoint nodes at this interval (metres). |
| `osmHoldingPositionSnapM` | number | `25.0` | Maximum radius (m) to snap an OSM stop-bar node onto the nearest taxiway waypoint and promote it to a HoldingPosition node. |
| `configHoldingPointSnapM` | number | `40.0` | Maximum radius (m) to snap a config holding-point polygon centroid onto the nearest taxiway waypoint. Larger than the OSM value because centroids may sit a few metres back from the taxiway edge. |

#### `edgeCosts` — base type multipliers

Applied at graph-build time to all edges of the corresponding aeroway type. Higher values make the router prefer other paths.

| Field | Type | Default | Description |
|---|---|---|---|
| `multIntersection` | number | `1.1` | Slight penalty for taxiway-intersection edges. |
| `multTaxilane` | number | `3.0` | Stand-access taxilane edges are strongly discouraged vs main taxiways. |
| `multRunway` | number | `20.0` | Runway edges are only traversed to vacate the runway; never preferred for taxi. |
| `multRunwayApproach` | number | `18.0` | Additional multiplier for edges arriving at a holding point / holding position node (approaching the runway threshold). Slightly below `multRunway` so vacating via the HP is still preferred over remaining on the runway. |
| `multWingspanAvoid` | number | `3.0` | Cost multiplier applied to `taxiWingspanAvoid` refs when the aircraft wingspan fits the avoid threshold. Higher values produce a stronger preference for the parallel narrower lane. |

#### `flowRules` — direction enforcement

Controls how heavily active taxiway flow rules penalise against-flow routing.

| Field | Type | Default | Description |
|---|---|---|---|
| `withFlowMaxDeg` | number | `45.0` | Bearing difference (°) at or below which an edge is considered to follow the active flow rule. |
| `withFlowMult` | number | `0.9` | Cost multiplier applied to edges that follow the active flow direction (< 1.0 gives a slight preference over uncontrolled taxiways). |
| `againstFlowMinDeg` | number | `135.0` | Bearing difference (°) at or above which an edge is considered against the flow rule. |
| `againstFlowMult` | number | `3.0` | Additional cost multiplier applied to edges that go against an active flow rule. |

#### `routing` — A\* search

| Field | Type | Default | Description |
|---|---|---|---|
| `hardTurnDeg` | number | `50.0` | Bearing change (°) above which an edge is hard-blocked during A\* within the same taxiway or between two non-intersection taxiways (prevents kinks and forces use of smooth intersection curves). |
| `wayrefChangePenalty` | number | `200.0` | Cost added when the route transitions from one named taxiway to another. |
| `forwardSnapM` | number | `120.0` | Radius (m) used to collect up to 3 forward start-node candidates for A\*. |
| `backwardSnapM` | number | `300.0` | Radius (m) used to collect up to 2 backward start-node candidates for A\*. |
| `heuristicWeight` | number | `1.0` | Weight applied to the A\* heuristic. Values above 1.0 are more goal-directed but may expand nodes sub-optimally; 1.0 is correct for small graphs. |
| `maxNodeExpansions` | integer | `5000` | Maximum number of nodes A\* expands before giving up. Higher values find better routes at greater CPU cost. |

#### `snapping` — interactive planning

Snap radii when the controller clicks to set a waypoint. Higher-priority types are checked first; the first match within the radius wins.

| Field | Type | Default | Priority | Description |
|---|---|---|---|---|
| `holdingPointM` | number | `30.0` | 1 (highest) | Snap to holding-point / holding-position nodes. |
| `intersectionM` | number | `15.0` | 2 | Snap to intersection waypoint nodes (labelled "Exit …"). |
| `suggestedRouteM` | number | `20.0` | 3 | Snap to the suggested route polyline. |
| `waypointM` | number | `40.0` | 4 (lowest) | Snap to any graph waypoint node. |

#### `targetSelection` — goal-node selection around the destination stand

Six-tier search used when routing to a stand approach point. Tiers are tried in order `(narrow, near) → (narrow, far) → (medium, near) → (medium, far) → (wide, near) → (wide, far)` and the first non-empty tier wins. Cones widen so closely aligned nodes are preferred; within each cone the near radius is tried before the far radius so the closest taxilane behind the stand beats a far-but-aligned one.

| Field | Type | Default | Description |
|---|---|---|---|
| `narrowConeDeg` | number | `10.0` | Tight cone (°) around the stand approach bearing; catches nodes closely aligned with the stand heading. |
| `mediumConeDeg` | number | `20.0` | Medium cone (°) used when no node is found inside `narrowConeDeg`. |
| `wideConeDeg` | number | `90.0` | Wide cone (°) used as a last resort; prevents picking nodes behind the stand (> 90° off the approach axis). |
| `nearRadiusM` | number | `80.0` | Near radius (m) tried inside each cone before `farRadiusM`; covers taxilanes running immediately behind straight stands. |
| `farRadiusM` | number | `170.0` | Far radius (m) tried inside each cone after `nearRadiusM`; covers diagonal stands and edge cases where the nearest taxilane is further out. |

#### `safety` — taxi safety monitoring

| Field | Type | Default | Description |
|---|---|---|---|
| `deviationThreshM` | number | `40.0` | Distance (m) an aircraft may deviate from its assigned route before a deviation warning is raised. |
| `endpointDeviationThreshM` | number | `80.0` | Relaxed deviation threshold (m) used while the aircraft is within `endpointRadiusM` of the route's first or last node — accounts for stands and holding points sitting slightly off the graph. |
| `endpointRadiusM` | number | `60.0` | Radius (m) around the first/last route node within which `endpointDeviationThreshM` replaces `deviationThreshM`. |
| `minSpeedKt` | number | `3.0` | Minimum ground speed (kt) required before safety checks are evaluated. |
| `maxPredictS` | number | `60.0` | Maximum prediction horizon (s) used when building conflict-detection paths. |
| `conflictDeltaS` | number | `30.0` | Two aircraft at the same intersection are flagged as conflicting if their estimated arrival times differ by less than this value (s). |
| `sameDirDeg` | number | `45.0` | Bearing difference (°) below which two converging paths are considered same-direction and excluded from conflict alerts. |

```json
"taxiNetworkConfig": {
    "graph":     { "subdivisionIntervalM": 15.0, "osmHoldingPositionSnapM": 25.0, "configHoldingPointSnapM": 40.0 },
    "edgeCosts": { "multIntersection": 1.1, "multTaxilane": 3.0, "multRunway": 20.0, "multRunwayApproach": 18.0, "multWingspanAvoid": 3.0 },
    "flowRules": { "withFlowMaxDeg": 45.0, "withFlowMult": 0.9, "againstFlowMinDeg": 135.0, "againstFlowMult": 3.0 },
    "routing":   { "hardTurnDeg": 50.0, "wayrefChangePenalty": 200.0, "forwardSnapM": 120.0, "backwardSnapM": 300.0, "heuristicWeight": 1.0, "maxNodeExpansions": 5000 },
    "snapping":        { "holdingPointM": 30.0, "intersectionM": 15.0, "suggestedRouteM": 20.0, "waypointM": 40.0 },
    "targetSelection": { "narrowConeDeg": 10.0, "mediumConeDeg": 20.0, "wideConeDeg": 90.0, "nearRadiusM": 80.0, "farRadiusM": 170.0 },
    "safety":          { "deviationThreshM": 40.0, "endpointDeviationThreshM": 80.0, "endpointRadiusM": 60.0, "minSpeedKt": 3.0, "maxPredictS": 60.0, "conflictDeltaS": 30.0, "sameDirDeg": 45.0 }
}
```

### `runways`

Per-runway configuration keyed by runway designator.

| Field | Type | Description |
|---|---|---|
| `opposite` | string | Reciprocal runway designator (used for go-around detection) |
| `twrFreq` | string | Tower frequency for this runway |
| `goAroundFreq` | string | Go-around (approach) frequency for this runway |
| `width` | int | Runway width in metres (e.g. `45`) |
| `threshold` | object | `{ "lat": ..., "lon": ... }` — runway threshold coordinates |
| `thresholdElevationFt` | int | Threshold elevation in feet; overrides `fieldElevation` for TTT altitude gates. Omit or set to `0` to use `fieldElevation`. |
| `estimateBarSide` | string | Which side of the Approach Estimate bar this runway's aircraft appear on: `"left"` or `"right"`. Omit to exclude from the bar. |
| `sidGroups` | object | Group number → array of SID prefixes. Aircraft in the same group get 5 nm spacing instead of 3 nm. |
| `sidColors` | object | Colour name → array of SID prefixes. Valid colours: `green`, `orange`, `turq`, `purple`, `red`, `white`, `yellow`. SIDs omitted here default to white. |
| `holdingPoints` | object | Named holding point definitions (see below) |
| `vacatePoints` | object | Named vacate point definitions (see below) |
| `gpsApproachPaths` | array | Non-straight-in RNP approach paths for early TTT detection. Each entry defines a sequence of fixes with distance/altitude gates. Omit for straight-in approaches. |

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

## Testing

FlowX includes a companion **FlowXTests** project — a standalone Win32 console executable that runs unit tests against the core plugin logic without requiring a live EuroScope installation.

### Test framework

[doctest](https://github.com/doctest/doctest) v2.5.1 (single-header, bundled at `include/doctest/doctest.h`).

### Building

Open `FlowX.sln` in Visual Studio 2022 and build the **FlowXTests** project (`Debug|Win32` or `Release|Win32`). The output is `Debug\FlowXTests.exe` or `Release\FlowXTests.exe` in the solution root.

A post-build event copies the required fixture files into the output directory automatically:

| Fixture | Destination | Purpose |
|---|---|---|
| `config.json` (solution root) | `$(OutDir)` | Real LOWW airport configuration |
| `FlowXTests/fixtures/settings.json` | `$(OutDir)` | Test settings with `updateCheck: false` |
| `FlowXTests/fixtures/osm_taxiways_LOWW.json` | `$(OutDir)` | Snapshot of the OSM taxiway cache |
| `FlowXTests/fixtures/Groundradar/ICAO_Aircraft.json` | `$(SolutionDir)Groundradar\` | Aircraft wingspan data |
| `FlowXTests/fixtures/Groundradar/GRpluginStands.txt` | `$(SolutionDir)Groundradar\` | Stand polygon data |

### Running

```
Release\FlowXTests.exe
```

A non-zero exit code means one or more tests failed. Pass `--help` for doctest options (e.g. `--tc=*OSM*` to filter by name).

### What is tested

| File | Real source compiled | Coverage |
|---|---|---|
| `test_geometry.cpp` | `taxi_graph.h` | Haversine distance, bearing, bearing-diff, point-to-segment distance, segment intersection |
| `test_graph.cpp` | `taxi_graph.cpp` | Graph build from synthetic OSM data, A\* routing, flow-rule enforcement, wingspan hard-exclusion and soft-avoidance |
| `test_osm.cpp` | `osm_taxiways.cpp` | OSM JSON parsing (taxiways, taxilanes, runways, holding positions, stands) |
| `test_lookups.cpp` | `CFlowX_LookupsTools.cpp` | Point-in-polygon, colour parsing, holding-point annotation encoding, weight category ranking |
| `test_helpers.cpp` | *(helpers are inline/header-only)* | String split/join, trim, upper-case, frequency annotation formatting |
| `test_tags.cpp` | `CFlowX_Tags.cpp` | Tag text and colour generation (Push+Start helper, Same-SID, ADES, QNH marker) using EuroScope stubs |
| `test_settings.cpp` | `CFlowX_Settings.cpp` | `LoadSettings`, `LoadConfig`, `LoadAircraftData`, `LoadGroundRadarStands` against real fixture files; OSM cache load and TaxiGraph build from the LOWW snapshot |

### EuroScope stub

`FlowXTests/stubs/EuroScope/EuroScopePlugIn.h` shadows the real SDK header via include-path ordering. It provides the same types and method signatures as the production header, implemented as plain data-holder structs with no DLL linkage. Tests set member variables directly to simulate flight-plan and radar-target state.

### OSM cache snapshot

`FlowXTests/fixtures/osm_taxiways_LOWW.json` is a point-in-time snapshot of the LOWW taxiway data (232 ways, 47 holding positions). It is used to verify that the cache loader and TaxiGraph builder work end-to-end without a network connection. If the OSM data changes significantly and you regenerate the cache from EuroScope, copy the new `osm_taxiways_LOWW.json` from the plugin directory into `FlowXTests/fixtures/` and update the way/holding-position count assertions in `test_settings.cpp`.

---

## Contributing

If you have a suggestion or encountered a bug, please open an [issue](https://github.com/sushiat/FlowX/issues) on GitHub. Include a description of the problem, relevant logs, and steps to reproduce.

[Pull requests](https://github.com/sushiat/FlowX/pulls) are welcome. Please keep features reasonably generic — the plugin is intended to be configurable via `config.json` rather than hard-coded for a specific airport or vACC.

### Development setup

Primary development is done in **VS Code** with the Microsoft C/C++ extension and CMake Tools; **Visual Studio 2022** is also fully supported and remains the reference build environment.

- Set the environment variable `EUROSCOPE_ROOT` to the EuroScope install directory (not the executable itself) to enable the debugger launch configuration in VS2022
- Avoid breakpoints during live controlling — use `.flowx debug` instead
- Target: 32-bit or 64-bit DLL (`Release|Win32` or `Release|x64`), C++23, Windows SDK 11.0

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
