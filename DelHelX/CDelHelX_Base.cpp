/**
 * @file CDelHelX_Base.cpp
 * @brief Plugin base layer; registers tag items, tag functions, and instantiates the radar screen.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#include "pch.h"
#include "CDelHelX_Base.h"

#include <filesystem>

#include "constants.h"
#include "helpers.h"
#include "date/tz.h"

/// @brief Registers all tag item types, tag functions, display type, and flight-plan lists with EuroScope.
CDelHelX_Base::CDelHelX_Base() : EuroScopePlugIn::CPlugIn(
                                     EuroScopePlugIn::COMPATIBILITY_CODE,
                                     PLUGIN_NAME,
                                     PLUGIN_VERSION,
                                     PLUGIN_AUTHOR,
                                     PLUGIN_LICENSE)
{
    this->debug       = false;
    this->radarScreen = nullptr;

    std::filesystem::path base(GetPluginDirectory());
    base.append("tzdata");
    date::set_install(base.string());

    this->RegisterTagItemType("Push+Start Helper", TAG_ITEM_PS_HELPER);
    this->RegisterTagItemType("Taxi out?", TAG_ITEM_TAXIOUT);
    this->RegisterTagItemType("New QNH", TAG_ITEM_NEWQNH);
    this->RegisterTagItemType("Same SID", TAG_ITEM_SAMESID);
    this->RegisterTagItemType("ADES Type-Y", TAG_ITEM_ADES);

    this->RegisterTagItemFunction("Set ONFREQ/STUP/PUSH", TAG_FUNC_ON_FREQ);
    this->RegisterTagItemFunction("Clear new QNH", TAG_FUNC_CLEAR_NEWQNH);

    this->RegisterDisplayType(PLUGIN_NAME, true, false, false, false);
}

/// @brief Called by EuroScope when a new radar screen is created.
/// @param sDisplayName Display name of the screen configuration.
/// @param NeedRadarContent Whether radar content is required.
/// @param GeoReferenced Whether the screen is geo-referenced.
/// @param CanBeSaved Whether the screen layout can be saved.
/// @param CanBeCreated Whether the screen can be created by the user.
/// @return Pointer to the newly created RadarScreen instance.
EuroScopePlugIn::CRadarScreen* CDelHelX_Base::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
    this->radarScreen        = new RadarScreen();
    this->radarScreen->debug = this->debug;
    return this->radarScreen;
}

/// @brief Pushes the flight plan strip to all online DEL, GND, and TWR controllers.
/// @param fp Flight plan to distribute.
void CDelHelX_Base::PushToOtherControllers(EuroScopePlugIn::CFlightPlan& fp) const
{
    for (EuroScopePlugIn::CController c = this->ControllerSelectFirst(); c.IsValid(); c = this->ControllerSelectNext(c))
    {
        if (c.IsController())
        {
            std::string callsign = c.GetCallsign();
            if (callsign.size() >= 3)
            {
                if (callsign.find("DEL") != std::string::npos || callsign.find("GND") != std::string::npos || callsign.find("TWR") != std::string::npos)
                {
                    fp.PushFlightStrip(c.GetCallsign());
                }
            }
        }
    }
}
