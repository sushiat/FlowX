#pragma once

#include <regex>

#define PLUGIN_NAME    "DelHelX"
#define PLUGIN_VERSION "0.4.0"
#define PLUGIN_AUTHOR  "Markus Korbel"
#define PLUGIN_LICENSE "(c) 2025, MIT License"
#define PLUGIN_LATEST_VERSION_URL "https://raw.githubusercontent.com/sushiat/DelHelX/master/version.txt"
#define PLUGIN_LATEST_DOWNLOAD_URL "https://github.com/sushiat/DelHelX/releases/latest"

const char SETTINGS_DELIMITER = '|';

const int TAG_ITEM_PS_HELPER = 1;
const int TAG_ITEM_TAXIOUT = 2;
const int TAG_ITEM_NEWQNH = 3;
const int TAG_ITEM_SAMESID = 4;
const int TAG_ITEM_HP1 = 5;
const int TAG_ITEM_HP2 = 6;
const int TAG_ITEM_HP3 = 7;
const int TAG_ITEM_HPO = 8;
const int TAG_ITEM_TAKEOFF_TIMER = 9;
const int TAG_ITEM_TAKEOFF_DISTANCE = 10;
const int TAG_ITEM_ASSIGNED_RUNWAY = 11;

const int TAG_FUNC_ON_FREQ = 100;
const int TAG_FUNC_CLEAR_NEWQNH = 101;
const int TAG_FUNC_ASSIGN_HP1 = 102;
const int TAG_FUNC_ASSIGN_HP2 = 103;
const int TAG_FUNC_ASSIGN_HP3 = 104;
const int TAG_FUNC_REQUEST_HP1 = 105;
const int TAG_FUNC_REQUEST_HP2 = 106;
const int TAG_FUNC_REQUEST_HP3 = 107;
const int TAG_FUNC_ASSIGN_HPO = 108;
const int TAG_FUNC_REQUEST_HPO = 109;
const int TAG_FUNC_HPO_LISTSELECT = 110;
const int TAG_FUNC_LINE_UP = 111;
const int TAG_FUNC_TAKE_OFF = 112;
const int TAG_FUNC_TRANSFER_NEXT = 113;

const COLORREF TAG_COLOR_NONE = 0;
const COLORREF TAG_COLOR_RED = RGB(200, 0, 0);
const COLORREF TAG_COLOR_ORANGE = RGB(255, 165, 0);
const COLORREF TAG_COLOR_GREEN = RGB(0, 200, 0);
const COLORREF TAG_COLOR_TURQ = RGB(64, 224, 208);
const COLORREF TAG_COLOR_PURPLE = RGB(182, 66, 245);
const COLORREF TAG_COLOR_WHITE = RGB(255, 255, 255);

constexpr auto TOPSKY_PLUGIN_NAME = "TopSky plugin";
const int TOPSKY_TAG_FUNC_SET_CLEARANCE_FLAG = 667;
