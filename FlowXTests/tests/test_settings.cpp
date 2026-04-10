#include "pch.h"
// test_settings.cpp
// Unit tests for CFlowX_Settings file-loading methods: LoadSettings, LoadConfig,
// LoadAircraftData, LoadGroundRadarStands, and the OSM cache load + graph build.
//
// CFlowX_Settings is compiled from real source.  File paths are resolved via
// GetPluginDirectory(), which returns the EXE directory.  The PostBuildEvent in
// FlowXTests.vcxproj copies fixture files to the correct locations:
//
//   $(OutDir)settings.json              ← fixtures/settings.json
//   $(OutDir)config.json                ← $(SolutionDir)config.json
//   $(OutDir)osm_taxiways_LOWW.json     ← fixtures/osm_taxiways_LOWW.json
//   $(SolutionDir)Groundradar\          ← fixtures/Groundradar/*

#include <doctest/doctest.h>
#include <thread>
#include "CFlowX_Tags.h"   // pulls in the full chain, including CFlowX_Settings

// ─── Test accessor ────────────────────────────────────────────────────────────

/// @brief Thin subclass that exposes the protected Settings members for inspection.
class SettingsTestAccessor : public CFlowX_Tags
{
  public:
    using CFlowX_Settings::airports;
    using CFlowX_Settings::aircraftWingspans;
    using CFlowX_Settings::grStands;
    using CFlowX_Settings::updateCheck;
    using CFlowX_Settings::autoParked;
    using CFlowX_Settings::autoScratchpadClear;
    using CFlowX_Settings::hpAutoScratch;
    using CFlowX_Settings::bgOpacity;
    using CFlowX_Settings::soundAirborne;
    using CFlowX_Settings::soundGndTransfer;
    using CFlowX_Settings::soundReadyTakeoff;
    using CFlowX_Settings::soundNoRoute;
    using CFlowX_Settings::soundTaxiConflict;
    using CFlowX_Settings::fontOffset;
    using CFlowX_Settings::approachEstWindowX;
    using CFlowX_Settings::approachEstWindowY;
    using CFlowX_Settings::approachEstWindowW;
    using CFlowX_Settings::approachEstWindowH;
    using CFlowX_Settings::approachEstVisible;
    using CFlowX_Settings::apprEstColors;
    using CFlowX_Settings::twrOutboundWindowX;
    using CFlowX_Settings::twrOutboundWindowY;
    using CFlowX_Settings::twrOutboundVisible;
    using CFlowX_Base::debug;
    using CFlowX_Logging::flashOnMessage;
    using CFlowX_Settings::PollOsmFuture;
    using CFlowX_Settings::PollGraphFuture;

    /// @brief Blocks until both the OSM cache load and the TaxiGraph build complete.
    void DrainAsyncWork()
    {
        while (IsOsmBusy())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PollOsmFuture();   // moves osmData in; starts graphFuture_

        while (IsGraphBusy())
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PollGraphFuture(); // moves osmGraph in
    }
};

// ─── Global accessor ─────────────────────────────────────────────────────────
// Constructed once.  DrainAsyncWork() blocks until the OSM cache load and graph
// build both complete so that OSM/graph tests can assert on the final state.

static SettingsTestAccessor& accessor()
{
    struct Init {
        SettingsTestAccessor acc;
        Init() { acc.DrainAsyncWork(); }
    };
    static Init init;
    return init.acc;
}

// ─── LoadSettings ─────────────────────────────────────────────────────────────

TEST_CASE("LoadSettings - updateCheck is false (prevents async HTTP in constructor)")
{
    CHECK(accessor().updateCheck == false);
}

TEST_CASE("LoadSettings - debug and flashOnMessage are false")
{
    CHECK(accessor().debug         == false);
    CHECK(accessor().flashOnMessage == false);
}

TEST_CASE("LoadSettings - autoParked is false (non-default)")
{
    // Default in header is true; fixture overrides to false.
    CHECK(accessor().autoParked == false);
}

TEST_CASE("LoadSettings - autoScratchpadClear is true (non-default)")
{
    // Default in header is false; fixture overrides to true.
    CHECK(accessor().autoScratchpadClear == true);
}

TEST_CASE("LoadSettings - bgOpacity is 75 (non-default)")
{
    // Default in header is 100; fixture overrides to 75.
    CHECK(accessor().bgOpacity == 75);
}

TEST_CASE("LoadSettings - fontOffset is 2 (non-default)")
{
    // Default in header is 0; fixture overrides to 2.
    CHECK(accessor().fontOffset == 2);
}

TEST_CASE("LoadSettings - soundAirborne is false (non-default)")
{
    // Default in header is true; fixture overrides to false.
    CHECK(accessor().soundAirborne == false);
}

TEST_CASE("LoadSettings - sound flags that remain default-true")
{
    CHECK(accessor().soundGndTransfer  == true);
    CHECK(accessor().soundReadyTakeoff == true);
    CHECK(accessor().soundNoRoute      == true);
    CHECK(accessor().soundTaxiConflict == true);
}

TEST_CASE("LoadSettings - twrOutboundWindow position loaded correctly")
{
    CHECK(accessor().twrOutboundWindowX == 100);
    CHECK(accessor().twrOutboundWindowY == 200);
    CHECK(accessor().twrOutboundVisible == false);
}

TEST_CASE("LoadSettings - approachEstWindow position and size loaded correctly")
{
    CHECK(accessor().approachEstWindowX == 50);
    CHECK(accessor().approachEstWindowY == 100);
    CHECK(accessor().approachEstWindowW == 300);
    CHECK(accessor().approachEstWindowH == 450);
    CHECK(accessor().approachEstVisible == true);
    CHECK(accessor().apprEstColors      == true);
}

// ─── LoadConfig ───────────────────────────────────────────────────────────────

TEST_CASE("LoadConfig - LOWW airport is present")
{
    CHECK(accessor().airports.count("LOWW") == 1);
}

TEST_CASE("LoadConfig - LOWW field elevation and basic frequencies")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.fieldElevation   == 600);
    CHECK(ap.gndFreq          == "121.6");
    CHECK(ap.airborneTransfer == 1500);
}

TEST_CASE("LoadConfig - LOWW ctrStations loaded (7 entries)")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.ctrStations.size() == 7);
}

TEST_CASE("LoadConfig - geoGndFreq west zone present with correct frequency")
{
    const auto& ap = accessor().airports.at("LOWW");
    REQUIRE(ap.geoGndFreq.count("west") == 1);
    CHECK(ap.geoGndFreq.at("west").freq == "121.775");
}

TEST_CASE("LoadConfig - defaultAppFreq loaded")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.defaultAppFreq == "134.675");
}

TEST_CASE("LoadConfig - nightTimeSids contains IRGO entry")
{
    const auto& ap = accessor().airports.at("LOWW");
    REQUIRE(ap.nightTimeSids.count("IRGO") == 1);
    CHECK(ap.nightTimeSids.at("IRGO") == "IRGOT");
}

TEST_CASE("LoadConfig - napReminder enabled at hour 20")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.nap_reminder.enabled == true);
    CHECK(ap.nap_reminder.hour    == 20);
}

TEST_CASE("LoadConfig - runway 11 present with correct twrFreq and opposite")
{
    const auto& ap = accessor().airports.at("LOWW");
    REQUIRE(ap.runways.count("11") == 1);
    const auto& rwy = ap.runways.at("11");
    CHECK(rwy.twrFreq  == "119.4");
    CHECK(rwy.opposite == "29");
}

TEST_CASE("LoadConfig - runway 11 has assignable holding point A12")
{
    const auto& rwy = accessor().airports.at("LOWW").runways.at("11");
    REQUIRE(rwy.holdingPoints.count("A12") == 1);
    CHECK(rwy.holdingPoints.at("A12").assignable == true);
}

TEST_CASE("LoadConfig - taxiFlowGeneric has 2 rules")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.taxiFlowGeneric.size() == 2);
}

TEST_CASE("LoadConfig - taxiWays has 7 entries")
{
    const auto& ap = accessor().airports.at("LOWW");
    CHECK(ap.taxiWays.size() == 7);
}

// ─── LoadAircraftData ─────────────────────────────────────────────────────────

TEST_CASE("LoadAircraftData - five types loaded")
{
    CHECK(accessor().aircraftWingspans.size() == 5);
}

TEST_CASE("LoadAircraftData - A320 wingspan is 35.8 m")
{
    REQUIRE(accessor().aircraftWingspans.count("A320") == 1);
    CHECK(accessor().aircraftWingspans.at("A320") == doctest::Approx(35.8));
}

TEST_CASE("LoadAircraftData - B748 wingspan is 68.4 m (heavy)")
{
    REQUIRE(accessor().aircraftWingspans.count("B748") == 1);
    CHECK(accessor().aircraftWingspans.at("B748") == doctest::Approx(68.4));
}

TEST_CASE("LoadAircraftData - C172 wingspan is 11.0 m (light)")
{
    REQUIRE(accessor().aircraftWingspans.count("C172") == 1);
    CHECK(accessor().aircraftWingspans.at("C172") == doctest::Approx(11.0));
}

TEST_CASE("LoadAircraftData - A359 (no Wingspan in JSON) gets WTC-M average (~35.8 m)")
{
    // A320 + B737 are both M category with wingspan 35.8, so average is 35.8.
    REQUIRE(accessor().aircraftWingspans.count("A359") == 1);
    CHECK(accessor().aircraftWingspans.at("A359") == doctest::Approx(35.8));
}

// ─── LoadGroundRadarStands ────────────────────────────────────────────────────

TEST_CASE("LoadGroundRadarStands - two stands loaded (A92 and B05)")
{
    CHECK(accessor().grStands.size() == 2);
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 stand exists")
{
    CHECK(accessor().grStands.count("LOWW:A92") == 1);
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 has 4 coordinate vertices")
{
    const auto& s = accessor().grStands.at("LOWW:A92");
    CHECK(s.lat.size() == 4);
    CHECK(s.lon.size() == 4);
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 first coordinate parsed correctly (DMS)")
{
    // COORD:N048.07.14.709:E016.33.00.259
    // lat = 48 + 7/60 + 14.709/3600 ≈ 48.120753
    // lon = 16 + 33/60 + 0.259/3600 ≈ 16.550072
    const auto& s = accessor().grStands.at("LOWW:A92");
    CHECK(s.lat[0] == doctest::Approx(48.0 + 7.0 / 60.0 + 14.709 / 3600.0).epsilon(1e-5));
    CHECK(s.lon[0] == doctest::Approx(16.0 + 33.0 / 60.0 + 0.259 / 3600.0).epsilon(1e-5));
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 has 2 blocks")
{
    const auto& s = accessor().grStands.at("LOWW:A92");
    CHECK(s.blocks.size() == 2);
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 block A93 has minWingspan 31.99")
{
    const auto& blocks = accessor().grStands.at("LOWW:A92").blocks;
    auto it = std::find_if(blocks.begin(), blocks.end(),
                           [](const standBlock& b) { return b.standName == "A93"; });
    REQUIRE(it != blocks.end());
    CHECK(it->minWingspan == doctest::Approx(31.99));
}

TEST_CASE("LoadGroundRadarStands - LOWW:A92 block A96 has minWingspan 0 (always blocked)")
{
    const auto& blocks = accessor().grStands.at("LOWW:A92").blocks;
    auto it = std::find_if(blocks.begin(), blocks.end(),
                           [](const standBlock& b) { return b.standName == "A96"; });
    REQUIRE(it != blocks.end());
    CHECK(it->minWingspan == doctest::Approx(0.0));
}

TEST_CASE("LoadGroundRadarStands - LOWW:B05 stand exists with 4 vertices and no blocks")
{
    REQUIRE(accessor().grStands.count("LOWW:B05") == 1);
    const auto& s = accessor().grStands.at("LOWW:B05");
    CHECK(s.lat.size() == 4);
    CHECK(s.blocks.empty());
}

// ─── OSM cache load ───────────────────────────────────────────────────────────
// The fixture osm_taxiways_LOWW.json is a snapshot of the real LOWW OSM cache.
// DrainAsyncWork() (called at accessor construction) blocks until PollOsmFuture
// has moved the data into osmData and PollGraphFuture has moved the built graph
// into osmGraph.

TEST_CASE("OSM cache - total way count matches snapshot (216)")
{
    CHECK(accessor().osmData.ways.size() == 216);
}

TEST_CASE("OSM cache - holding position count matches snapshot (47)")
{
    CHECK(accessor().osmData.holdingPositions.size() == 47);
}

TEST_CASE("OSM cache - all seven main LOWW taxiway refs are present")
{
    const auto& ways = accessor().osmData.ways;
    for (const auto& ref : {"D", "E", "L", "M", "P", "Q", "W"})
    {
        bool found = std::any_of(ways.begin(), ways.end(),
                                 [&](const OsmWay& w) { return w.ref == ref; });
        CHECK_MESSAGE(found, "taxiway ref not found: ", ref);
    }
}

TEST_CASE("OSM cache - holding position A12 is present")
{
    const auto& hps = accessor().osmData.holdingPositions;
    bool found = std::any_of(hps.begin(), hps.end(),
                             [](const OsmHoldingPosition& h) { return h.ref == "A12"; });
    CHECK(found);
}

TEST_CASE("OSM cache - ways include all expected aeroway types")
{
    const auto& ways = accessor().osmData.ways;
    auto hasType = [&](AerowayType t)
    {
        return std::any_of(ways.begin(), ways.end(), [t](const OsmWay& w) { return w.type == t; });
    };
    CHECK(hasType(AerowayType::Taxiway));
    CHECK(hasType(AerowayType::Taxilane));
    CHECK(hasType(AerowayType::Taxiway_HoldingPoint));
    CHECK(hasType(AerowayType::Taxiway_Intersection));
    CHECK(hasType(AerowayType::Runway));
}

// ─── TaxiGraph build ──────────────────────────────────────────────────────────

TEST_CASE("TaxiGraph - graph is non-empty after cache load")
{
    CHECK(accessor().osmGraph.NodeCount() > 0);
}

TEST_CASE("TaxiGraph - node count is substantial (> 500 for LOWW)")
{
    // 216 ways with long segments subdivided every 15 m yields well over 500 nodes.
    CHECK(accessor().osmGraph.NodeCount() > 500);
}
