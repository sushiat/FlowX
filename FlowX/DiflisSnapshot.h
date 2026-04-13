/**
 * @file DiflisSnapshot.h
 * @brief Per-tick immutable snapshot of all DIFLIS-window draw inputs.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include "DiflisModel.h"
#include <string>
#include <vector>

struct DiflisAirportConfig;

/// @brief Per-tick immutable snapshot of everything DrawDiflisWindow reads from
/// volatile plugin state.
///
/// Built on the EuroScope main thread once per OnRefresh tick and consumed by the
/// draw function. Step 3 of the DIFLIS rendering decoupling will move draw onto
/// the popout thread, at which point this snapshot becomes the sole handoff.
///
/// The airport config pointer is a raw non-owning pointer because
/// DiflisAirportConfig lives in CFlowX_Settings for the plugin lifetime and is
/// not hot-swappable (mirrors the existing DrawDiflisWindow cache pattern).
struct DiflisSnapshot
{
    int                           winW          = 0;       ///< Popout client width in pixels
    int                           winH          = 0;       ///< Popout client height in pixels
    bool                          isTopmost     = true;    ///< PopoutWindow::IsTopmost() at build time
    bool                          isMaximized   = false;   ///< PopoutWindow::IsMaximized() at build time
    const DiflisAirportConfig*    airportConfig = nullptr; ///< Non-owning; plugin-lifetime config for myIcao
    std::string                   myIcao;                  ///< Primary airport ICAO (empty when no airport configured)
    std::vector<DiflisStripCache> strips;                  ///< Copy of RadarScreen::diflisStripsCache
    std::string                   qnhLabel;                ///< QNH status-bar label (leading Q/A already stripped); "-" when unknown
    std::string                   atisLetter;              ///< Current ATIS letter for myIcao; "-" when unknown
    std::vector<double>           onlineFreqs;             ///< Primary frequencies of all online controllers (rating > 1)
    double                        myFreq = 0.0;            ///< My own primary frequency; 0 when not logged in
    std::string                   dragCallsign;            ///< Callsign currently being dragged; empty when none
    std::string                   dragFromGroup;           ///< Group id the dragged strip came from; empty when none
};
