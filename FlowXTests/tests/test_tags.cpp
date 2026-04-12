#include "pch.h"
// test_tags.cpp
// Unit tests for CFlowX_Tags tag-generation methods using the EuroScope stub.
//
// TagTestAccessor inherits from CFlowX_Tags, exposes the protected tag methods
// as public, and gives tests direct access to the protected state maps so they
// can set up flight-plan and airport data without going through the real ES API.

#include <doctest/doctest.h>
#include "CFlowX_Tags.h"
#include "constants.h"

// ─── Test accessor ────────────────────────────────────────────────────────────

/// @brief Thin subclass of CFlowX_Tags that:
///   - exposes protected tag methods as public wrappers
///   - re-exports the protected state maps so tests can populate them
class TagTestAccessor : public CFlowX_Tags
{
  public:
    // Expose state that tests need to pre-populate
    using CFlowX_Tags::airports;
    using CFlowX_Tags::adesCache;
    using CFlowX_Tags::twrSameSID_flightPlans;
    using CFlowX_Tags::flightStripAnnotation;

    // Public wrappers around the protected tag methods
    tagInfo TestGetSameSidTag(EuroScopePlugIn::CFlightPlan& fp)
    {
        return GetSameSidTag(fp);
    }

    tagInfo TestGetPushStartHelperTag(EuroScopePlugIn::CFlightPlan& fp,
                                      EuroScopePlugIn::CRadarTarget& rt)
    {
        return GetPushStartHelperTag(fp, rt);
    }

    tagInfo TestGetAdesTag(EuroScopePlugIn::CFlightPlan& fp)
    {
        return GetAdesTag(fp);
    }

    tagInfo TestGetNewQnhTag(EuroScopePlugIn::CFlightPlan& fp)
    {
        return GetNewQnhTag(fp);
    }
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Build a stub flight plan ready for tag queries.
static EuroScopePlugIn::CFlightPlan MakeFP(
    const std::string& callsign,
    const std::string& origin   = "LOWW",
    const std::string& rwy      = "16",
    const std::string& sid      = "ABBA1G",
    const std::string& squawk   = "7521",
    bool               cleared  = true)
{
    EuroScopePlugIn::CFlightPlan fp;
    fp.valid_            = true;
    fp.callsign_         = callsign;
    fp.fpData_.origin    = origin;
    fp.fpData_.departureRwy = rwy;
    fp.fpData_.sidName   = sid;
    fp.clearanceFlag_    = cleared;
    fp.assignedData_.squawk = squawk;
    return fp;
}

// Build a stub radar target whose pilot squawk matches the given string.
static EuroScopePlugIn::CRadarTarget MakeRT(const std::string& callsign,
                                             const std::string& squawk)
{
    EuroScopePlugIn::CRadarTarget rt;
    rt.valid_      = true;
    rt.callsign_   = callsign;
    rt.posData_.valid_  = true;
    rt.posData_.squawk_ = squawk;
    return rt;
}

// Build an airport config with one runway and a set of SID colour mappings.
static airport MakeAirportConfig()
{
    runway rwy16;
    rwy16.designator = "16";
    rwy16.twrFreq    = "119.400";
    // Map SID keys (first N-2 chars of the SID name) to colours
    rwy16.sidColors["ABBA"] = "green";
    rwy16.sidColors["NEMO"] = "orange";

    airport ap;
    ap.icao    = "LOWW";
    ap.gndFreq = "121.600";
    ap.runways["16"] = rwy16;
    return ap;
}

// ─── GetSameSidTag ────────────────────────────────────────────────────────────

TEST_CASE("GetSameSidTag - airport not in config returns empty tag")
{
    TagTestAccessor acc;
    // airports map is empty by default

    auto fp = MakeFP("AUA1", "ZZZZ", "16", "ABBA1G");
    tagInfo t = acc.TestGetSameSidTag(fp);
    CHECK(t.tag.empty());
}

TEST_CASE("GetSameSidTag - short SID (≤2 chars) returns empty tag")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    auto fp = MakeFP("AUA1", "LOWW", "16", "AB"); // too short
    tagInfo t = acc.TestGetSameSidTag(fp);
    CHECK(t.tag.empty());
}

TEST_CASE("GetSameSidTag - known SID returns name and configured colour")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    // SID "ABBA1G" → key "ABBA" → green
    auto fp = MakeFP("AUA1", "LOWW", "16", "ABBA1G");
    tagInfo t = acc.TestGetSameSidTag(fp);
    CHECK(t.tag == "ABBA1G");
    CHECK(t.color == TAG_COLOR_GREEN);
}

TEST_CASE("GetSameSidTag - different SID gets different colour")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    // SID "NEMO1G" → key "NEMO" → orange
    auto fp = MakeFP("AUA2", "LOWW", "16", "NEMO1G");
    tagInfo t = acc.TestGetSameSidTag(fp);
    CHECK(t.tag == "NEMO1G");
    CHECK(t.color == TAG_COLOR_ORANGE);
}

TEST_CASE("GetSameSidTag - SID not in sidColors map returns white")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    // SID "UNKN1G" → key "UNKN" not in sidColors → TAG_COLOR_WHITE
    auto fp = MakeFP("AUA3", "LOWW", "16", "UNKN1G");
    tagInfo t = acc.TestGetSameSidTag(fp);
    CHECK(t.tag == "UNKN1G");
    CHECK(t.color == TAG_COLOR_WHITE);
}

TEST_CASE("GetSameSidTag - departed aircraft has dimmed colour (55%)")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();
    // Mark the callsign as having taken off (tick > 0)
    acc.twrSameSID_flightPlans["AUA4"] = 1000;

    auto fp = MakeFP("AUA4", "LOWW", "16", "ABBA1G");
    tagInfo t = acc.TestGetSameSidTag(fp);
    // Original green is RGB(0,200,0); dimmed to 55% → RGB(0,110,0)
    COLORREF expected = RGB(0, GetGValue(TAG_COLOR_GREEN) * 55 / 100, 0);
    CHECK(t.color == expected);
}

// ─── GetPushStartHelperTag ────────────────────────────────────────────────────

TEST_CASE("GetPushStartHelperTag - airport not in config returns empty green tag")
{
    TagTestAccessor acc;
    // airports empty

    auto fp = MakeFP("AUA5", "ZZZZ");
    auto rt = MakeRT("AUA5", "7521");
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag.empty());
    CHECK(t.color == TAG_COLOR_GREEN);
}

TEST_CASE("GetPushStartHelperTag - aircraft already moving (groundState set) returns empty")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    auto fp = MakeFP("AUA6", "LOWW");
    fp.groundState_ = "TAXI";
    auto rt = MakeRT("AUA6", "7521");
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag.empty());
}

TEST_CASE("GetPushStartHelperTag - no departure runway shows !RWY in red")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    auto fp = MakeFP("AUA7", "LOWW", "" /* no rwy */, "ABBA1G", "7521", false);
    auto rt = MakeRT("AUA7", "7521");
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag == "!RWY");
    CHECK(t.color == TAG_COLOR_RED);
}

TEST_CASE("GetPushStartHelperTag - no squawk assigned shows !ASSR in red")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    auto fp = MakeFP("AUA8", "LOWW", "16", "ABBA1G", "" /* no squawk */, false);
    auto rt = MakeRT("AUA8", "0000");
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag == "!ASSR");
    CHECK(t.color == TAG_COLOR_RED);
}

TEST_CASE("GetPushStartHelperTag - no clearance flag shows !CLR in red")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    auto fp = MakeFP("AUA9", "LOWW", "16", "ABBA1G", "7521", false /* no clearance */);
    auto rt = MakeRT("AUA9", "7521");
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag == "!CLR");
    CHECK(t.color == TAG_COLOR_RED);
}

TEST_CASE("GetPushStartHelperTag - squawk mismatch shows assigned squawk in orange")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    // Configure stub controller as observer (facility 0, rating 0)
    // so the function doesn't short-circuit to "OK"
    EuroScopePlugIn::TestControllerMyself() = EuroScopePlugIn::CController{};

    auto fp = MakeFP("AUA10", "LOWW", "16", "ABBA1G", "7521", true);
    auto rt = MakeRT("AUA10", "1234"); // pilot squawk differs from assigned
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    // radarScreen is nullptr → returns early after setting the squawk
    CHECK(t.tag == "7521");
    CHECK(t.color == TAG_COLOR_ORANGE);
}

TEST_CASE("GetPushStartHelperTag - GND controller sees OK")
{
    TagTestAccessor acc;
    acc.airports["LOWW"] = MakeAirportConfig();

    // Configure stub controller as GND (facility=3, rating=3, isController=true)
    EuroScopePlugIn::CController& me = EuroScopePlugIn::TestControllerMyself();
    me.valid_        = true;
    me.isController_ = true;
    me.facility_     = 3; // GND
    me.rating_       = 3;

    auto fp = MakeFP("AUA11", "LOWW", "16", "ABBA1G", "7521", true);
    auto rt = MakeRT("AUA11", "7521"); // squawk matches
    tagInfo t = acc.TestGetPushStartHelperTag(fp, rt);
    CHECK(t.tag == "OK");
    CHECK(t.color == TAG_COLOR_GREEN);
}

// ─── GetAdesTag ───────────────────────────────────────────────────────────────

TEST_CASE("GetAdesTag - cache hit returns cached tag")
{
    TagTestAccessor acc;

    tagInfo cached;
    cached.tag   = "EDDF";
    cached.color = TAG_COLOR_WHITE;
    acc.adesCache["AUA20"] = cached;

    auto fp = MakeFP("AUA20");
    fp.fpData_.destination = "EDDF";
    tagInfo t = acc.TestGetAdesTag(fp);
    CHECK(t.tag == "EDDF");
    CHECK(t.color == TAG_COLOR_WHITE);
}

TEST_CASE("GetAdesTag - cache miss falls back to raw destination")
{
    TagTestAccessor acc;
    // adesCache is empty

    auto fp = MakeFP("AUA21");
    fp.fpData_.destination = "LSZH";
    tagInfo t = acc.TestGetAdesTag(fp);
    CHECK(t.tag == "LSZH");
    CHECK(t.color == TAG_COLOR_DEFAULT_NONE);
}

// ─── GetNewQnhTag ─────────────────────────────────────────────────────────────

TEST_CASE("GetNewQnhTag - annotation slot 8 starts with Q shows orange X")
{
    TagTestAccessor acc;

    auto fp = MakeFP("AUA30");
    fp.assignedData_.annotations[8] = "Q122750";
    tagInfo t = acc.TestGetNewQnhTag(fp);
    CHECK(t.tag == "X");
    CHECK(t.color == TAG_COLOR_ORANGE);
}

TEST_CASE("GetNewQnhTag - empty annotation shows no tag")
{
    TagTestAccessor acc;

    auto fp = MakeFP("AUA31");
    fp.assignedData_.annotations[8] = "";
    tagInfo t = acc.TestGetNewQnhTag(fp);
    CHECK(t.tag.empty());
}

TEST_CASE("GetNewQnhTag - annotation without Q flag shows no tag")
{
    TagTestAccessor acc;

    auto fp = MakeFP("AUA32");
    fp.assignedData_.annotations[8] = " 122750M1"; // no Q at [0]
    tagInfo t = acc.TestGetNewQnhTag(fp);
    CHECK(t.tag.empty());
}
