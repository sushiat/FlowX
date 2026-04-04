/**
 * @file CFlowX_Base.h
 * @brief Declaration of CFlowX_Base, the plugin registration and radar screen creation layer.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once
#include <EuroScope/EuroScopePlugIn.h>

#include "RadarScreen.h"

/// @brief Base plugin layer responsible for EuroScope registration and radar-screen lifecycle.
///
/// Inherits from EuroScopePlugIn::CPlugIn and registers all tag item types, tag functions,
/// display types, and flight-plan lists used by FlowX.
class CFlowX_Base : public EuroScopePlugIn::CPlugIn
{
  protected:
    bool                             debug;       ///< True when verbose debug logging is enabled
    RadarScreen*                     radarScreen; ///< Pointer to the active radar screen instance, or nullptr
    EuroScopePlugIn::CFlightPlanList tttInbound;  ///< Flight-plan list for the TWR Inbound (TTT) panel
    EuroScopePlugIn::CFlightPlanList twrSameSID;  ///< Flight-plan list for the TWR Outbound panel

    /// @brief Pushes the given flight plan strip to all online DEL, GND, and TWR controllers.
    /// @param fp Flight plan whose strip should be distributed.
    void PushToOtherControllers(EuroScopePlugIn::CFlightPlan& fp) const;

  public:
    /// @brief Constructs the plugin, registering all tag items, functions, and flight-plan lists with EuroScope.
    CFlowX_Base();

    /// @brief Returns the current debug-mode state.
    [[nodiscard]] bool GetDebug() const { return this->debug; }

    /// @brief Clears the stored radar screen pointer (called by the screen itself before deletion).
    void ClearRadarScreen()
    {
        radarScreen = nullptr;
    }

    /// @brief Called by EuroScope when a new radar screen is created; instantiates a RadarScreen object.
    /// @param sDisplayName Display name of the new screen configuration.
    /// @param NeedRadarContent Whether radar content is required.
    /// @param GeoReferenced Whether the screen is geo-referenced.
    /// @param CanBeSaved Whether the screen layout can be saved.
    /// @param CanBeCreated Whether the screen can be created by the user.
    /// @return Pointer to the newly created RadarScreen instance.
    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;
};
