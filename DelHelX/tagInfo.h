/**
 * @file tagInfo.h
 * @brief Declaration of tagInfo, carrying tag display text and color for EuroScope tag rendering.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <string>

/// @brief Holds the text and colour for a single EuroScope tag item cell.
struct tagInfo {
    std::string tag;                            ///< Text to display in the tag cell (max 15 chars for EuroScope)
    COLORREF color  = TAG_COLOR_DEFAULT_GRAY;   ///< RGB colour of the tag text
};
