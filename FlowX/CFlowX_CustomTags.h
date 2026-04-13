/**
 * @file CFlowX_CustomTags.h
 * @brief Declaration of CFlowX_CustomTags, the custom GDI window cache builder layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include "CFlowX_Tags.h"
#include "cachedTagData.h"

/// @brief Plugin layer that builds and maintains the custom GDI window row caches
/// (TWR Outbound, TWR Inbound) by computing all tag data in single-pass combined functions.
class CFlowX_CustomTags : public CFlowX_Tags
{
  protected:
    /// @brief Computes all inbound fields for one row in a single pass.
    /// Sets row.sortKey to the raw "RWY_MM:SS" for ordering; row.ttt is left empty
    /// (caller fills it with the gap-adjusted display value after sorting).
    /// Also fills: nm, vacate, arrRwy.
    void ComputeInboundCacheEntry(const std::string&             tttKey,
                                  EuroScopePlugIn::CFlightPlan&  fp,
                                  EuroScopePlugIn::CRadarTarget& rt,
                                  TwrInboundRowCache&            row);

    /// @brief Computes all timer-updated fields for one outbound row in a single pass,
    /// performing shared lookups (airport, rwy, pos, annotation) only once.
    /// Fills: status, depInfo, rwy, sameSid, nextFreq, hp, spacing, liveSpacing, sortKey, dimmed.
    void ComputeOutboundCacheEntry(EuroScopePlugIn::CFlightPlan&  fp,
                                   EuroScopePlugIn::CRadarTarget& rt,
                                   TwrOutboundRowCache&           row);

    /// @brief Pre-calculates all custom-window row caches and the DEP/H departure-rate window.
    /// Called every second from OnTimer.
    void UpdateTagCache();

  public:
    /// @brief Rebuilds only the DIFLIS strip cache (diflisStripsCache) for the primary airport.
    /// Extracted from UpdateTagCache so drag-and-drop handlers can force a synchronous rebuild
    /// after mutating diflisOverrides, avoiding the ~1 s "snap back to origin" before the next
    /// OnTimer tick catches up.
    void RebuildDiflisStripCache();

    /// @brief Refreshes TTT and NM for an inbound aircraft on each position update.
    /// Updates the corresponding row in twrInboundRowsCache directly.
    void UpdatePositionDerivedTags(EuroScopePlugIn::CRadarTarget rt);
};
