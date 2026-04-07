/**
 * @file tagInfo.h
 * @brief Declaration of tagInfo, carrying tag display text and color for EuroScope tag rendering.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <string>

/// @brief Holds the display attributes for a single tag item cell.
/// @note bgColor, bold, and fontDelta are only honoured in GDI custom list windows (DrawTwrOutbound / DrawTwrInbound).
///       The EuroScope native tag API supports foreground colour only.
struct tagInfo {
    std::string tag;                              ///< Text to display in the tag cell (max 15 chars for EuroScope)
    COLORREF    color     = TAG_COLOR_DEFAULT_GRAY; ///< RGB foreground colour
    COLORREF    bgColor   = TAG_COLOR_DEFAULT_NONE; ///< RGB background fill; TAG_COLOR_DEFAULT_NONE = no fill
    bool        bold      = false;                  ///< Bold font weight for this cell
    int         fontDelta = 0;                      ///< Signed px size offset applied to the row's base font (capped ±4 in renderer)
};
