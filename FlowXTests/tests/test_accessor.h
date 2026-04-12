#pragma once
// test_accessor.h
// Shared test accessor that exposes protected CFlowX_Settings members for
// inspection.  Used by test_settings.cpp and test_taxi_routes.cpp.

#include <thread>
#include "CFlowX_Tags.h"

/// @brief Thin subclass that exposes the protected Settings members for inspection.
class SettingsTestAccessor : public CFlowX_Tags
{
  public:
    using CFlowX_Base::debug;
    using CFlowX_Logging::flashOnMessage;
    using CFlowX_Settings::aircraftWingspans;
    using CFlowX_Settings::airports;
    using CFlowX_Settings::apprEstColors;
    using CFlowX_Settings::approachEstVisible;
    using CFlowX_Settings::approachEstWindowH;
    using CFlowX_Settings::approachEstWindowW;
    using CFlowX_Settings::approachEstWindowX;
    using CFlowX_Settings::approachEstWindowY;
    using CFlowX_Settings::autoParked;
    using CFlowX_Settings::autoScratchpadClear;
    using CFlowX_Settings::bgOpacity;
    using CFlowX_Settings::fontOffset;
    using CFlowX_Settings::grStands;
    using CFlowX_Settings::hpAutoScratch;
    using CFlowX_Settings::PollGraphFuture;
    using CFlowX_Settings::PollOsmFuture;
    using CFlowX_Settings::soundAirborne;
    using CFlowX_Settings::soundGndTransfer;
    using CFlowX_Settings::soundNoRoute;
    using CFlowX_Settings::soundReadyTakeoff;
    using CFlowX_Settings::soundTaxiConflict;
    using CFlowX_Settings::twrOutboundVisible;
    using CFlowX_Settings::twrOutboundWindowX;
    using CFlowX_Settings::twrOutboundWindowY;
    using CFlowX_Settings::updateCheck;

    /// @brief Blocks until both the OSM cache load and the TaxiGraph build complete.
    void DrainAsyncWork()
    {
        while (IsOsmBusy())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PollOsmFuture(); // moves osmData in; starts graphFuture_

        while (IsGraphBusy())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PollGraphFuture(); // moves osmGraph in
    }
};

/// @brief Returns the global singleton SettingsTestAccessor.
/// Constructed once; DrainAsyncWork() blocks until the OSM cache load and graph
/// build both complete so that OSM/graph tests can assert on the final state.
SettingsTestAccessor& accessor();
