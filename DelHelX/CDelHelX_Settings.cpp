#include "pch.h"
#include "CDelHelX_Settings.h"
#include "helpers.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <semver/semver.hpp>

CDelHelX_Settings::CDelHelX_Settings()
{
	this->updateCheck = false;

	this->LoadSettings();
	this->LoadConfig();

	if (this->updateCheck) {
		this->latestVersion = std::async(FetchLatestVersion);
	}
}

/// @brief Loads persisted plugin settings from EuroScope's settings store.
void CDelHelX_Settings::LoadSettings()
{
	const char* settings = this->GetDataFromSettings(PLUGIN_NAME);
	if (settings) {
		std::vector<std::string> splitSettings = split(settings, SETTINGS_DELIMITER);

		if (splitSettings.size() < 3) {
			this->LogMessage("Invalid saved settings found, reverting to default.", "Settings");

			this->SaveSettings();

			return;
		}

		std::istringstream(splitSettings[0]) >> this->updateCheck;
		std::istringstream(splitSettings[1]) >> this->flashOnMessage;
		std::istringstream(splitSettings[2]) >> this->debug;

		this->LogMessage("Successfully loaded settings.", "Settings");
	}
	else {
		this->LogMessage("No saved settings found, using defaults.", "Settings");
	}
}

/// @brief Serialises current settings and writes them to EuroScope's settings store.
void CDelHelX_Settings::SaveSettings()
{
	std::ostringstream ss;
	ss << this->updateCheck << SETTINGS_DELIMITER
		<< this->flashOnMessage << SETTINGS_DELIMITER
		<< this->debug;

	this->SaveDataToSettings(PLUGIN_NAME, "DelHelX settings", ss.str().c_str());
}

/// @brief Parses config.json from the plugin directory and populates the airports map.
void CDelHelX_Settings::LoadConfig()
{
	json config;
	try
	{
		std::filesystem::path base(GetPluginDirectory());
		base.append("config.json");

		std::ifstream ifs(base.c_str());

		config = json::parse(ifs);
	}
	catch (std::exception& e)
	{
		this->LogMessage("Failed to read config. Error: " + std::string(e.what()), "Config");
		return;
	}

	for (auto& [icao, json_airport] : config.items())
	{
		// Get basic airport attributes
		airport ap{
			icao,
			json_airport.value<std::string>("gndFreq", ""),
			json_airport.value<std::string>("twrFreq", "")
		};
		ap.fieldElevation = json_airport.value<int>("fieldElevation", 0);
		ap.airborneTransfer = json_airport.value<int>("airborneTransfer", 0);
		ap.airborneTransferWarning = json_airport.value<int>("airborneTransferWarning", 0);

		auto ctrStations{ json_airport["ctrStations"].get<std::vector<std::string>>() };
		ap.ctrStations = ctrStations;

		json json_geoGnds;
		try
		{
			json_geoGnds = json_airport.at("geoGndFreq");
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to get geographic ground frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [name, json_geoGnd] : json_geoGnds.items())
		{
			geoGndFreq ggf{
				name,
				json_geoGnd.value<std::string>("freq", "")
			};

			auto lat{ json_geoGnd["lat"].get<std::vector<double>>() };
			auto lon{ json_geoGnd["lon"].get<std::vector<double>>() };
			ggf.lat = lat;
			ggf.lon = lon;

			ap.geoGndFreq.emplace(name, ggf);
		}

		json json_rwyTwrs;
		try
		{
			json_rwyTwrs = json_airport.at("rwyTwrFreq");
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to get runway tower frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [rwyid, json_rwy] : json_rwyTwrs.items())
		{
			auto rwyFreq = json_rwy.value<std::string>("freq", "");
			ap.rwyTwrFreq.emplace(rwyid, rwyFreq);
		}

		json json_taxiouts;
		try
		{
			json_taxiouts = json_airport.at("taxiOutStands");
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to get taxi out stands for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		for (auto& [name, json_taxiout] : json_taxiouts.items())
		{
			taxiOutStands tos{ name };

			auto lat{ json_taxiout["lat"].get<std::vector<double>>() };
			auto lon{ json_taxiout["lon"].get<std::vector<double>>() };
			tos.lat = lat;
			tos.lon = lon;

			ap.taxiOutStands.emplace(name, tos);
		}

		json json_nap_reminder;
		try
		{
			json_nap_reminder = json_airport.at("napReminder");
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to load NAP reminder config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
			continue;
		}

		napReminder reminder{
			json_nap_reminder.value<bool>("enabled", false),
			json_nap_reminder.value<int>("hour", 0),
			json_nap_reminder.value<int>("minute", 0),
			json_nap_reminder.value<std::string>("tzone", ""),
			false
		};
		ap.nap_reminder = reminder;

		// Load night SIDs
		try
		{
			for (auto& [sidKey, nightName] : json_airport.at("nightTimeSids").items())
			{
				ap.nightTimeSids.emplace(sidKey, nightName.get<std::string>());
			}
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to load night-time SID replacements for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
		}

		// Load SID-specific approach frequencies
		try
		{
			ap.defaultAppFreq = json_airport.value<std::string>("defaultAppFreq", "");
			for (auto& [freq, sids] : json_airport.at("sidAppFreqs").items())
			{
				ap.sidAppFreqs.emplace(freq, sids.get<std::vector<std::string>>());
			}
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to load SID-specific frequencies for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
		}

		// Load runway configurations
		try
		{
			for (auto& [rwyDesignator, json_rwy] : json_airport.at("runways").items())
			{
				runway rwy{};
				rwy.designator = rwyDesignator;
				rwy.opposite = json_rwy.value<std::string>("opposite", "");
				rwy.thresholdLat = json_rwy["threshold"].value<double>("lat", 0.0);
				rwy.thresholdLon = json_rwy["threshold"].value<double>("lon", 0.0);

				for (auto& [groupKey, sids] : json_rwy["sidGroups"].items())
				{
					int groupNum = std::stoi(groupKey);
					for (auto& sid : sids)
						rwy.sidGroups.emplace(sid.get<std::string>(), groupNum);
				}

				for (auto& [color, sids] : json_rwy["sidColors"].items())
				{
					for (auto& sid : sids)
						rwy.sidColors.emplace(sid.get<std::string>(), color);
				}

				for (auto& [hpName, json_hp] : json_rwy["holdingPoints"].items())
				{
					holdingPoint hp{};
					hp.name = hpName;
					hp.index = json_hp.value<int>("index", 0);
					hp.assignable = json_hp.value<bool>("assignable", false);
					hp.sameAs = json_hp.value<std::string>("sameAs", "");
					hp.lat = json_hp["polygon"]["lat"].get<std::vector<double>>();
					hp.lon = json_hp["polygon"]["lon"].get<std::vector<double>>();
					rwy.holdingPoints.emplace(hpName, hp);
				}

				if (json_rwy.contains("vacatePoints"))
				{
					for (auto& [vpName, json_vp] : json_rwy["vacatePoints"].items())
					{
						vacatePoint vp{};
						vp.minGap = json_vp.value<double>("minGap", 0.0);
						vp.stands = json_vp["stands"].get<std::vector<std::string>>();
						rwy.vacatePoints.emplace(vpName, vp);
					}
				}

				ap.runways.emplace(rwyDesignator, rwy);
			}
		}
		catch (std::exception& e)
		{
			this->LogMessage("Failed to load runway config for airport \"" + icao + "\". Error: " + std::string(e.what()), "Config");
		}

		this->airports.emplace(icao, ap);
	}

	this->LogMessage("Successfully loaded config for " + std::to_string(this->airports.size()) + " airport(s).", "Config");

	for (auto& airport : this->airports)
	{
		this->LogDebugMessage("Airport: " + airport.first, "Config");
		this->LogDebugMessage("--> GND: " + airport.second.gndFreq, "Config");
		this->LogDebugMessage("--> TWR: " + airport.second.twrFreq, "Config");
		int ctrIndex = 0;
		for (const auto& ctr : airport.second.ctrStations)
		{
			this->LogDebugMessage("--> CTR[" + std::to_string(ctrIndex) + "]: " + ctr, "Config");
			ctrIndex++;
		}
		for (auto& geoGnd : airport.second.geoGndFreq)
		{
			this->LogDebugMessage("--> GeoGnd " + geoGnd.first, "Config");
			this->LogDebugMessage("----> FRQ: " + geoGnd.second.freq, "Config");
			std::string lat_string = std::accumulate(std::begin(geoGnd.second.lat), std::end(geoGnd.second.lat), std::string(),
				[](const std::string& ss, const double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LAT: " + lat_string, "Config");
			std::string lon_string = std::accumulate(std::begin(geoGnd.second.lon), std::end(geoGnd.second.lon), std::string(),
				[](const std::string& ss, const double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LON: " + lon_string, "Config");
		}
		for (auto& twrRwy : airport.second.rwyTwrFreq)
		{
			this->LogDebugMessage("--> TWR[" + twrRwy.first + "]: " + twrRwy.second, "Config");
		}
		for (auto& taxiOut : airport.second.taxiOutStands)
		{
			this->LogDebugMessage("--> TaxiOut " + taxiOut.first, "Config");
			std::string lat_string = std::accumulate(std::begin(taxiOut.second.lat), std::end(taxiOut.second.lat), std::string(),
				[](const std::string& ss, const double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LAT: " + lat_string, "Config");
			std::string lon_string = std::accumulate(std::begin(taxiOut.second.lon), std::end(taxiOut.second.lon), std::string(),
				[](const std::string& ss, const double s)
				{
					return ss.empty() ? std::to_string(s) : ss + ", " + std::to_string(s);
				});
			this->LogDebugMessage("----> LON: " + lon_string, "Config");
		}
		this->LogDebugMessage("---> NAP reminder: Enabled=" + std::to_string(airport.second.nap_reminder.enabled) + ", Hour=" + std::to_string(airport.second.nap_reminder.hour) + ", Minute=" + std::to_string(airport.second.nap_reminder.minute) + ", TZone=" + airport.second.nap_reminder.tzone, "Config");
	}

}

/// @brief Resolves the latestVersion future and logs a message if a newer version is available.
void CDelHelX_Settings::CheckForUpdate()
{
	try
	{
		semver::version latest{ this->latestVersion.get() };
		semver::version current{ PLUGIN_VERSION };

		if (latest > current) {
			std::string info = "A new version (" + latest.to_string() + ") of " + PLUGIN_NAME + " is available, download it at " + PLUGIN_LATEST_DOWNLOAD_URL;
			this->LogMessage(info, "Update");
		}
	}
	catch (std::exception& e)
	{
		MessageBox(NULL, e.what(), PLUGIN_NAME, MB_OK | MB_ICONERROR);
	}

	this->latestVersion = std::future<std::string>();
}