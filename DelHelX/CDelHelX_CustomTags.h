#pragma once
#include "CDelHelX_Tags.h"
#include "cachedTagData.h"

/// @brief Plugin layer that builds and maintains the custom GDI window row caches
/// (TWR Outbound, TWR Inbound) by computing all tag data in single-pass combined functions.
class CDelHelX_CustomTags : public CDelHelX_Tags
{
protected:
    /// @brief Computes all timer-updated fields for one outbound row in a single pass,
    /// performing shared lookups (airport, rwy, pos, annotation) only once.
    /// Fills: status, depInfo, rwy, sameSid, nextFreq, hp, spacing, sortKey, dimmed.
    void ComputeOutboundCacheEntry(EuroScopePlugIn::CFlightPlan& fp,
                                   EuroScopePlugIn::CRadarTarget& rt,
                                   TwrOutboundRowCache& row);

    /// @brief Computes all inbound fields for one row in a single pass.
    /// Sets row.sortKey to the raw "RWY_MM:SS" for ordering; row.ttt is left empty
    /// (caller fills it with the gap-adjusted display value after sorting).
    /// Also fills: nm, vacate, arrRwy.
    void ComputeInboundCacheEntry(const std::string& tttKey,
                                   EuroScopePlugIn::CFlightPlan& fp,
                                   EuroScopePlugIn::CRadarTarget& rt,
                                   TwrInboundRowCache& row);

    /// @brief Pre-calculates all custom-window row caches and the DEP/H departure-rate window.
    /// Called every second from OnTimer.
    void UpdateTagCache();

public:
    /// @brief Refreshes TTT and NM for an inbound aircraft on each position update.
    /// Updates the corresponding row in twrInboundRowsCache directly.
    void UpdatePositionDerivedTags(EuroScopePlugIn::CRadarTarget rt);
};
