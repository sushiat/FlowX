// pch.h — Minimal precompiled header for the FlowXTests project.
// Defines PCH_H to suppress the MFC-heavy FlowX/pch.h when shared
// source files include it via their own relative #include "pch.h".

#ifndef PCH_H
#define PCH_H

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>   // COLORREF, RECT, etc. — needed by constants.h / stubs
#include <cmath>
#include <format>
#include <numbers>
#include <ranges>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>

#endif // PCH_H
