/**
 * @file helpers.h
 * @brief HTTP fetch helpers, exception classes, and general string/math utility functions.
 * @author Markus Korbel
 * @copyright (c) 2026, MIT License
 */

#pragma once

#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <WinInet.h>

#include "constants.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

/// @brief Fetches the latest plugin version string from the remote version file.
/// @return Version string (e.g. "0.6.0") retrieved over HTTP, or throws on failure.
/// @note Performs a synchronous WinInet HTTP request; call from a background thread.
std::string FetchLatestVersion();

/// @brief Fetches the full VATSIM v3 data feed JSON.
/// @return Raw JSON string from VATSIM_DATA_URL, or throws on failure.
/// @note Performs a synchronous WinInet HTTP request; call from a background thread.
std::string FetchVatsimData();

/// @brief Fetches and parses VATSIM ATIS data, returning only the airports in the given list.
/// @param airports Upper-case ICAO codes to look for.
/// @return Map of ICAO → atis_code; airports with no ATIS present are absent from the map.
/// @note Performs an HTTP request and JSON parse entirely on the calling thread; call from a background thread.
std::map<std::string, std::string> FetchAtisData(std::vector<std::string> airports);

/// @brief Base class for all plugin exceptions that can display a Win32 MessageBox.
class flowxexception : public std::exception
{
public:
    /// @brief Constructs the exception with the given message string.
    /// @param what Human-readable error description.
    explicit flowxexception(const std::string& what) : std::exception{ what.c_str() } {}

    /// @brief Returns the Win32 MB_ICON* constant appropriate for this exception type.
    /// @return Win32 message-box icon flag (e.g. MB_ICONERROR).
    virtual const long icon() const = 0;

    /// @brief Shows a Win32 MessageBox with the exception message and appropriate icon.
    inline void whatMessageBox()
    {
        MessageBoxA(NULL, this->what(), PLUGIN_NAME, MB_OK | icon());
    }
};

/// @brief Exception type for fatal errors, shown with an error icon.
class error : public flowxexception
{
public:
    /// @brief Constructs an error exception.
    /// @param what Human-readable error description.
    explicit error(const std::string& what) : flowxexception{ what } {}

    /// @brief Returns MB_ICONERROR.
    const long icon() const override
    {
        return MB_ICONERROR;
    }
};

/// @brief Exception type for non-fatal warnings, shown with a warning icon.
class warning : public flowxexception
{
public:
    /// @brief Constructs a warning exception.
    /// @param what Human-readable warning description.
    explicit warning(const std::string& what) : flowxexception{ what } {}

    /// @brief Returns MB_ICONWARNING.
    const long icon() const override
    {
        return MB_ICONWARNING;
    }
};

/// @brief Exception type for informational messages, shown with an information icon.
class information : public flowxexception
{
public:
    /// @brief Constructs an information exception.
    /// @param what Human-readable information message.
    explicit information(std::string& what) : flowxexception{ what } {}

    /// @brief Returns MB_ICONINFORMATION.
    const long icon() const override
    {
        return MB_ICONINFORMATION;
    }
};

/// @brief Returns the directory containing the loaded plugin DLL.
/// @return Absolute path string of the plugin's directory, without trailing separator.
inline std::string GetPluginDirectory()
{
    char buf[MAX_PATH] = { 0 };
    GetModuleFileName(HINSTANCE(&__ImageBase), buf, MAX_PATH);

    std::string::size_type pos = std::string(buf).find_last_of("\\/");

    return std::string(buf).substr(0, pos);
}

/// @brief Splits a string into tokens using the given delimiter character.
/// @param s Input string to split.
/// @param delim Delimiter character (default: space).
/// @return Vector of token strings in order of appearance.
inline std::vector<std::string> split(const std::string& s, char delim = ' ')
{
    std::istringstream ss(s);
    std::string item;
    std::vector<std::string> res;

    while (std::getline(ss, item, delim)) {
        res.push_back(item);
    }

    return res;
}

/// @brief Joins a vector of strings into a single string separated by the given delimiter.
/// @param s Vector of strings to join.
/// @param delim Delimiter character inserted between tokens (default: space).
/// @return Concatenated string with delimiter appended after every element including the last.
inline std::string join(const std::vector<std::string>& s, const char delim = ' ')
{
    std::ostringstream ss;
    std::copy(s.begin(), s.end(), std::ostream_iterator<std::string>(ss, &delim));
    return ss.str();
}

/// @brief Checks whether a string begins with a given prefix.
/// @param str String to test.
/// @param pre Prefix to look for.
/// @return True if @p str starts with @p pre.
inline bool starts_with(const std::string& str, const std::string& pre)
{
    return str.rfind(pre, 0) == 0;
}

/// @brief Converts all characters of a string to upper-case in place.
/// @param str String to convert; modified in place.
inline void to_upper(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) -> unsigned char { return std::toupper(c); });
}

/// @brief Formats a VHF frequency as a 6-character dot-free annotation token.
/// @param freq Frequency value (e.g. 121.6, 122.800).
/// @return 6-character string with the dot removed and 3 decimal digits normalised (e.g. "121600").
inline std::string freqToAnnotation(double freq)
{
    std::string s = std::format("{:.3f}", freq);
    s.erase(std::remove(s.begin(), s.end(), '.'), s.end());
    return s;
}

/// @brief Formats a VHF frequency string as a 6-character dot-free annotation token.
/// @param freq Frequency string in any format (e.g. "121.6", "121.600", "122.800").
/// @return 6-character string with the dot removed and 3 decimal digits normalised (e.g. "121600").
inline std::string freqToAnnotation(const std::string& freq)
{
    try   { return freqToAnnotation(std::stod(freq)); }
    catch (...) { return freq; } // caller will notice the wrong length and can handle it
}

/// @brief Rounds an integer to the nearest multiple of @p closest.
/// @param num Value to round.
/// @param closest Rounding unit (must be > 0).
/// @return @p num rounded to the nearest multiple of @p closest.
inline int round_to_closest(int num, int closest)
{
    return ((num + closest / 2) / closest) * closest;
}

/// @brief Removes leading and trailing characters from a character set.
/// @param str Input string.
/// @param charset Set of characters to strip (default: space and tab).
/// @return Trimmed copy of @p str, or an empty string if @p str consists only of charset characters.
inline std::string trim(const std::string& str, const std::string& charset = " \t\r\n")
{
    size_t begin = str.find_first_not_of(charset);
    if (begin == std::string::npos) {
        // Empty string or only characters from charset provided
        return "";
    }

    size_t end = str.find_last_not_of(charset);

    return str.substr(begin, end - begin + 1);
}
