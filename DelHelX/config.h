#pragma once

#include <string>
#include <map>
#include <vector>

struct geoGndFreq
{
	std::string name;
	std::string freq;
	std::vector<double> lat = {};
	std::vector<double> lon = {};
};

struct taxiOutStands
{
	std::string name;
	std::vector<double> lat = {};
	std::vector<double> lon = {};
};

struct napReminder
{
	bool enabled;
	int hour;
	int minute;
	std::string tzone;
	bool triggered;
};

struct holdingPoint
{
	std::string name;
	int index = 0;
	bool assignable = false;
	std::string sameAs;
	std::vector<double> lat = {};
	std::vector<double> lon = {};
};

struct vacatePoint
{
	double minGap = 0.0;
	std::vector<std::string> stands = {};
};

struct runway
{
	std::string designator;
	double thresholdLat = 0.0;
	double thresholdLon = 0.0;
	std::map<std::string, holdingPoint> holdingPoints = {};
	std::map<std::string, int> sidGroups = {};
	std::map<std::string, std::string> sidColors = {};
	std::map<std::string, vacatePoint> vacatePoints = {};
};

struct airport
{
	std::string icao;
	std::string gndFreq;
	std::string twrFreq;
	std::string appFreq;
	int fieldElevation = 0;

	std::map<std::string, geoGndFreq> geoGndFreq = {};
	std::map<std::string, std::string> rwyTwrFreq = {};
	std::vector<std::string> ctrStations = {};
	std::map<std::string, taxiOutStands> taxiOutStands = {};
	napReminder nap_reminder = {};
	std::map<std::string, std::string> nightTimeSids = {};
	std::map<std::string, std::string> sidAppFreqs = {};
	std::map<std::string, runway> runways = {};
};
