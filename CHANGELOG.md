# Changelog

All notable changes to FlowX are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- DIFLIS (Digital Flight Strip) popout window with drag-and-drop reordering, strip override undo, status bar (QNH, controller, action buttons), and arrival ETA for inbounds
- Settings window accessible from the Start menu, scales with font offset
- FlowX icon in the Start menu
- GND tail dots overlay for visualising ground traffic trails
- Preferred-route overrides for taxi routing with origin filter
- Precise holding-point goal nodes via edge splitting and HP-goal forcing
- Auto-assign runway holding point when a taxi clearance is accepted
- Accept the suggested taxi route with X1 or X2 mouse button
- Automated version numbering derived from git tags at build time

### Changed
- Start menu slimmed and reorganised; Windows submenu collapses on toggle
- DIFLIS is now popout-only (in-screen drag/resize state removed)
- Popout windows close when their visibility is toggled off from the Start menu

### Fixed
- HP config-file order not respected when assigning holding points (#35)

## [0.7.0] - 2026-04-12

### Added
- Interactive taxi route planning with A\* pathfinding and magenta route preview on the radar screen
- OSM-based taxi graph built from downloaded aeroway data, cached per airport ICAO
- Taxi overlay: graph edges, runway centrelines, holding-position labels, intersection labels, flow chevrons
- Taxi safety monitoring: conflict and deviation detection with coloured markers, warning tags, and sound alerts
- Show individual taxi route on radar target hover
- Push zone reservations with on-network walk-graph conflict detection and configurable sound toggle
- Pushback route planning with taxi blocking and dead-end detection
- No-route sound (`noRoute.wav`) when a path cannot be found
- Taxilane swingover: ALT-toggle s-bend crossover between parallel taxilanes
- Taxi wingspanAvoid soft penalty to prefer wider taxiways for narrow-body aircraft
- Runway vacation exit restrictions configurable per runway with WTC filtering
- Config-based runway centrelines replace OSM aeroway=runway download
- Emergency squawk detection and scratchpad exclusions
- Configurable `taxiOnlyZones` override for push/taxi stand detection
- `taxiNetworkConfig` section in per-airport config for routing tuning
- Unit test project (FlowXTests) using doctest with fixture-driven taxi route regression tests
- GitHub Actions build workflow (Win32/x86)
- Multi-airport OSM parametrization via `osmCenter`/`osmRadius` in config

### Changed
- Outbound taxi routes now target the assigned holding point instead of always routing full runway length
- A\* cost model overhauled: runway approach penalty, per-rule flow multipliers, wayref change penalty, junction priority, and debug tracing
- Goal node selection uses per-wayRef closest-to-destination with node merge promotion and directional cone
- Push reservation display gated behind `showTaxiRoutes`/hover to avoid noise

### Fixed
- Backward-exit shortcuts suppressed in taxi routing
- Cross-lane phantom node merges eliminated
- Forward-only start node selection and correct heading for parked aircraft
- `FindWaypointRoute` total-distance undercount due to goal-candidate snapping
- False CONFLICT for same-direction tailgating aircraft
- Taxi route not clearing when aircraft skips over final node or goes airborne
- GND transfer tag cleared when assigning a taxi route to an inbound

## [0.6.0] - 2026-04-07

### Added
- FlowX start button on the radar screen with popup Start menu
- Window visibility toggles, Save positions command, and X close buttons on all custom windows
- WX/ATIS window with live VATSIM ATIS polling (wind, QNH, RVR display)
- Global font size offset setting with dynamic window layout scaling
- Window background opacity setting with live adjustment via Start menu
- Occupied stand detection with Gate column colour coding
- GND transfer square for landed inbounds, ages yellow after 20 s and red after 30 s
- Departure queue position (`#`) column in the TWR outbound list
- Live departure distance (`dNM`) column in the TWR outbound list
- Sound alerts: departure separation loss (`!SEP`), ready-for-takeoff reminder for lined-up aircraft
- Dismiss QNH command in Start menu to bulk-clear QNH change markers
- Auto-PARKED state detection
- SID dot frequency-transfer click for GND controllers
- Approach-fix early detection for non-straight-in RNP approaches (LOWW runway 16)
- Exception logging to `debugLog.txt` for all EuroScope callbacks
- `.flowx debugstats` command for performance diagnostics

### Changed
- Project renamed from **DelHelX** to **FlowX**
- Plugin settings migrated from EuroScope delimiter strings to JSON
- Start menu reorganised with Assists and Notifications sections, click sound added
- Tags with custom background colours now use bold font
- TTT dirDiff cap lowered to 15° for tighter go-around cone
- Auto-Clear Scratch with configurable callsign exclusions
- Approach path config renamed to `gpsApproachPaths` with human-readable altitude offsets

### Fixed
- Main-thread freezes: ATIS JSON parsing offloaded, runway headings cached, HP polygon tests guarded
- `ProcessRedoFlagQueue` thread-safety: queue drained on main thread via `OnTimer`
- HP popup menus now sorted by config file order
- Go-around detection and lifecycle correctness improvements
- ATIS fetch crash on null callsign or ATIS code
- BECMG wind parsing bug
- Polygon buffer overflow in point-in-polygon tests
- FlowX button floating above true bottom edge when chat panel is closed
- TWR outbound sort: near-HP TAXI aircraft always ranked above far TAXI

## [0.5.0] - 2026-03-21

### Added
- TTT (time-to-takeoff) tracking for inbound traffic with arrival ETA column
- Go-around detection and lifecycle management with frequency transfer
- Departure spacing and timing tracking with distance colour coding
- Blinking tag support, used for departure frequency notification
- Auto stand assignment via tag function and timer
- TWR outbound list sort key

### Changed
- Departure info locks at airborne state; distance display suppressed while on ground
- Frequency transfer falls back to searching by frequency when station name lookup fails

### Fixed
- Inbound TTT state cleanup on disconnect
- Crash when next controller is unavailable during transfer
- Go-around lifecycle edge cases

## [0.4.0] - 2026-03-13

### Added
- Tower same-SID tracking list
- Departure info tag rendered directly on the radar screen (moved from scratchpad)
- Auto holding point assignment
- Holding point and transfer info shown in the custom departure tag
- Scratchpad HP shortcuts for TWR controllers
- Custom GDI departure windows with configurable column layout

### Changed
- Night SID entries expanded to full names in config
- Flight strip annotation system overhauled; transfer state stored in annotation slot 4
- Config refactored for first round of SID grouping changes

### Fixed
- Holding point assignment override bug
- Radar screen null pointer errors on screen deletion
- 30 s green departure timer display

## [0.3.0] - 2025-04-02

### Added
- NAP (No ATIS Passed) reminder configurable via `config.json`

## [0.2.0] - 2025-04-01

### Added
- Debug mode toggle via `.flowx debug` chat command

## [0.1.0] - 2025-03-24

### Added
- Initial release (formerly DelHelX)
- `config.json` for airport-specific data: runways, holding points, SID groups, frequencies
- QNH change column with TopSky-safe annotation storage
- Taxi-out vs. push detection based on configurable stand polygons
- Departure management with same-SID tracking and holding point assignment
- `.flowx` debug chat commands

[Unreleased]: https://github.com/sushiat/FlowX/compare/0.7.0...HEAD
[0.7.0]: https://github.com/sushiat/FlowX/compare/0.6.0...0.7.0
[0.6.0]: https://github.com/sushiat/FlowX/compare/0.5.0...0.6.0
[0.5.0]: https://github.com/sushiat/FlowX/compare/0.4.0...0.5.0
[0.4.0]: https://github.com/sushiat/FlowX/compare/0.3.0...0.4.0
[0.3.0]: https://github.com/sushiat/FlowX/compare/0.2.0...0.3.0
[0.2.0]: https://github.com/sushiat/FlowX/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/sushiat/FlowX/releases/tag/0.1.0
