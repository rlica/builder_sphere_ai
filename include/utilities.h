#pragma once

#include "json.hpp"
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include "json-schema.hpp"
#include <cxxabi.h>
#include <chrono>
#include <iomanip>
#include <optional>
#include "tbb/enumerable_thread_specific.h"
#include <limits>
#include <filesystem>
#include <random>

using namespace std;

using namespace std::chrono;
using json = nlohmann::json;
using nlohmann::json_uri;
using nlohmann::json_schema::json_validator;

class loadException : public std::exception
{
private:
    std::string message_;

public:
    explicit loadException(const std::string &message)
    {
        message_ = message;
    };
    const char *what() const noexcept override
    {
        return message_.c_str();
    }
};

class driverException : public std::exception
{
private:
    std::string message_;

public:
    explicit driverException(const std::string &message)
    {
        message_ = message;
    };
    const char *what() const noexcept override
    {
        return message_.c_str();
    }
};

inline string util_demangle(string to_demangle)
{
    int status = 0;
    char *buff = __cxxabiv1::__cxa_demangle(to_demangle.c_str(), NULL, NULL, &status);
    string demangled = buff;
    free(buff);
    return demangled;
}

inline void unknownExceptionHandler(string catchLocation)
{
    cout << "Unknown exception type: '" << util_demangle(__cxxabiv1::__cxa_current_exception_type()->name()) << "'" << endl;
    cout << "catch location: " + catchLocation << endl;
    cout << "make a print screen and submit a bug request" << endl;
    abort();
}

inline json readConfigFile(string configFileName)
{
    std::ifstream configFile(configFileName.c_str());

    if (!configFile.is_open())
    {
        throw loadException("the selected config file '" + configFileName + "' does not exist");
    }

    stringstream paramStream;
    paramStream << configFile.rdbuf();

    json tempParam;
    try
    {
        tempParam = json::parse(paramStream.str());

        if (tempParam.contains("pvConfig"))
        {
            for (auto pv : tempParam["pvConfig"].items())
            {
                tempParam["pvConfig"][pv.key()]["pvName"] = string(tempParam["_id"]) + "-" + string(tempParam["pvConfig"][pv.key()]["pvName"]);
            }
        }
    }
    catch (json::parse_error &ex)
    {
        throw loadException("the configuration file cannot be parsed");
    }

    return tempParam;
}

// inline void validateJsonSchema(json configFile, json schemaFile)
// {
//     json_validator validator(nullptr, nlohmann::json_schema::default_string_format_check, nullptr);

//     try
//     {
//         validator.set_root_schema(schemaFile);
//     }
//     catch (const std::exception &exp)
//     {
//         throw loadException("validation of schema failed: " + string(exp.what()));
//     }

//     try
//     {
//         validator.validate(configFile);
//     }
//     catch (const std::exception &exp)
//     {
//         throw loadException("configFile validation of schema failed: " + string(exp.what()));
//     }
// }

// inline uint64_t getCrtTime()
// {
//     auto now = chrono::system_clock::now();
//     auto duration = now.time_since_epoch();
//     auto nanos = chrono::duration_cast<chrono::nanoseconds>(duration).count();

//     return uint64_t(nanos);
// }

inline string getStrTime()
{
    auto now = system_clock::now();
    time_t now_t = system_clock::to_time_t(now);

    stringstream ss;
    ss << put_time(localtime(&now_t), "%Y-%m-%d-%H-%M-%S");

    return ss.str();
}

template <typename S, typename V>
inline bool equalVector(const S vect1, const V vect2)
{

    if (vect1->size() != vect2->size())
    {
        return false;
    }

    for (uint i = 0; i < vect1->size(); i++)
    {
        if (vect1->at(i) != vect2->at(i))
        {
            return false;
        }
    }

    return true;
}

template <typename S, typename V>
inline bool equalVector3(const S vect1, const V vect2)
{

    if (vect1.size() != vect2.size())
    {
        return false;
    }

    for (uint i = 0; i < vect1.size(); i++)
    {

        if (vect1[i].size() != vect2[i].size())
        {
            return false;
        }

        for (uint o = 0; o < vect1[i].size(); o++)
        {
            if (vect1[i][o] != vect2[i][o])
            {
                return false;
            }
        }
    }

    return true;
}

inline double roundValue(double value, int decimal_places)
{
    const double multiplier = std::pow(10.0, decimal_places);
    return std::round(value * multiplier) / multiplier;
}

inline string getCurrentDate(time_t t = time(0))
{
    auto now = localtime(&t);

    string year = to_string((now->tm_year + 1900));

    string mon = "0" + to_string(now->tm_mon + 1);
    mon = mon.substr(mon.length() - 2);

    string day = "0" + to_string(now->tm_mday);
    day = day.substr(day.length() - 2);

    string hour = "0" + to_string(now->tm_hour);
    hour = hour.substr(hour.length() - 2);

    string min = "0" + to_string(now->tm_min);
    min = min.substr(min.length() - 2);

    string sec = "0" + to_string(now->tm_sec);
    sec = sec.substr(sec.length() - 2);

    string time = year + "-" + mon + "-" + day + " " + hour + "-" + min + "-" + sec;

    return time;
}

inline optional<string> getLastFile(string path, string ext)
{

    vector<string> files;

    for (auto const &entry : std::filesystem::directory_iterator{path})
    {
        if (entry.path().extension() == ext)
        {
            files.push_back(entry.path());
        }
    }

    if (files.size())
    {
        sort(files.begin(), files.end());

        return files.back();
    }

    return {};
}

inline void dumpJsonToPath(json jFile, string path)
{
    time_t now = system_clock::to_time_t(system_clock::now());

    string fileName = getCurrentDate(now);

    std::replace(fileName.begin(), fileName.end(), ' ', '-');

    fileName = path + "/" + fileName + ".json";

    ofstream outFile(fileName.c_str());
    outFile << std::setw(4) << jFile << std::endl;
}

inline json readJsonFile(string fileName)
{
    std::ifstream outFile(fileName);
    return json::parse(outFile);
}

inline std::vector<int> splitData(int n, int p)
{
    int fValue = n / p + 1;
    int fCount = n % p;

    vector<int> fVect = vector<int>(fCount, fValue);

    int sValue = n / p;
    int sCount = p - n % p;

    vector<int> sVect = vector<int>(sCount, sValue);

    fVect.insert(fVect.end(), sVect.begin(), sVect.end());
    return fVect;
}

class hist1D
{

public:
    hist1D() {};

    hist1D(uint nBins, double xMin, double xMax)
        : varNBins(nBins), varXMin(xMin), varXMax(xMax)
    {
        rng = std::mt19937(dev());
        varHistData = vector<int>(getNBins(), 0);

        distr = std::uniform_real_distribution<double>(-0.5, 0.5);
    }

    uint getNBins() { return varNBins; }

    double getXMin() { return varXMin; }
    void setXMin(double xMin) { varXMin = xMin; }

    double getXMax() { return varXMax; }
    void setXMax(double xMax) { varXMax = xMax; }

    uint getEntries() { return varEntries; }
    uint getUnderEntries() { return varUnderEntries; }
    uint getOverEntries() { return varOverEntries; }
    uint getIntegral() { return varEntries - (varUnderEntries + varOverEntries); }

    uint getBinContent(uint bin) { return varHistData.at(bin); }

    vector<int> getHistData() { return varHistData; }

    void clear()
    {

        varEntries = varUnderEntries = varOverEntries = 0;
        varHistData.assign(varHistData.size(), 0);
    }

    void normalizeHistogram()
    {
    }

    void setEnergyCalibration(vector<double> calib)
    {

        if (calib == varEnergyCalibration)
        {
            return;
        }

        clear();

        varEnergyCalibration = calib;

        if (calib.size())
        {
            double nMin = 0;
            double nMax = 0;

            for (uint i = 0; i < varEnergyCalibration.size(); i++)
            {
                nMin += varEnergyCalibration[i] * pow(varXMin, i);
                nMax += varEnergyCalibration[i] * pow(varXMax, i);
            }

            setXMin(nMin);
            setXMax(nMax);
        }
        else
        {
            setXMin(varDefaultXMin);
            setXMax(varDefaultXMax);
        }
    }

    uint64_t getZoneIntegral(vector<double> &zone)
    {

        auto sBin = findBin(zone[0]);
        auto eBin = findBin(zone[1]);

        return accumulate(varHistData.begin() + sBin, varHistData.begin() + eBin, 0);
    }

    uint findBin(double value)
    {
        return uint(varNBins * (value - varXMin) / (varXMax - varXMin));
    }

    void setBinContent(int bin, int val)
    {
        varEntries -= varHistData[bin];
        varEntries += val;
        varHistData[bin] = val;
    }

    double getBinCenter(int bin)
    {
        return varXMin + (varXMax - varXMin) * (bin + 0.5) / varNBins;
    }

    pair<int, double> fill(double value)
    {

        auto calibValue = calibrate(value);

        auto bin = findBin(calibValue);

        if (bin < 0)
        {
            varUnderEntries++;
            return make_pair(-1, calibValue);
        }
        else if (bin >= varNBins)
        {
            varOverEntries++;
            return make_pair(varNBins + 1, calibValue);
        }
        else
        {
            varEntries++;
            varHistData[bin]++;
            return make_pair(bin, calibValue);
        }
        return {};
    }

    double calibrate(double nrg)
    {

        if (varEnergyCalibration.size())
        {
            nrg += getRandom();

            double calib = 0;

            for (uint i = 0; i < varEnergyCalibration.size(); i++)
            {
                calib += varEnergyCalibration[i] * pow(nrg, i);
            }

            return calib;
        }

        return nrg;
    }

    double getRandom()
    {

        cout << "------------------" << distr(rng) << endl;

        return distr(rng);
    }

    vector<double> getNormData()
    {

        vector<double> normData = vector<double>(varHistData.size());

        transform(varHistData.begin(), varHistData.end(), normData.begin(), [this](auto &i)
                  { return static_cast<double>(i) / getIntegral(); });

        return normData;
    }

private:
    uint varNBins;
    double varXMin;
    double varXMax;

    double varDefaultXMin = 0;
    double varDefaultXMax = pow(2, 14);

    uint varEntries = 0;
    uint varUnderEntries = 0;
    uint varOverEntries = 0;

    vector<int> varHistData;
    vector<double> varEnergyCalibration;

    std::uniform_real_distribution<double> distr;
    std::random_device dev;
    std::mt19937 rng;
};

constexpr size_t hashstr(const char *str, size_t hash = 0)
{
    return *str ? hashstr(str + 1, (hash * 31) + *str) : hash;
}

inline auto clipVector(vector<int> v,
                       double min = std::numeric_limits<double>::min(),
                       double max = std::numeric_limits<double>::max()) -> vector<double>
{
    vector<double> outVect = vector<double>(v.size());

    transform(v.begin(), v.end(), outVect.begin(), [min, max](auto &i)
              { 
                if(i < min){
                    return min;                
                } else if(i > max){
                    return max;
                } else {
                    return static_cast<double>(i);
                } });

    return outVect;
}

struct calibPoint
{
    vector<double> calibParam;
    uint8_t lut;
    uint8_t type;
    uint32_t runNumber;
};
