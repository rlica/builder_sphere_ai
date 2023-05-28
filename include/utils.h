#ifndef utils_h
#define utils_h

#include <chrono>
#include <iostream>
#include <map>
#include "json-schema.hpp"
#include "json.hpp"

using namespace std;
using nlohmann::json_uri;
using nlohmann::json_schema::json_validator;
using json = nlohmann::json;

inline map<string, string> fileExtensions = {{"root", ".root"},
                                             {"3MV", ".root"},
                                             {"proto", ".cap"},
                                             {"cap", ".cap"},
                                             {"delila", ".root"},
                                             {"gasp", ""}};

class event
{

public:
    event() {}

    uint64_t getTimeStamp() { return timeStamp; }
    void setTimeStamp(uint64_t val) { timeStamp = val; }

    uint16_t getEnergy() { return energy; }
    void setEnergy(uint16_t nrg) { energy = nrg; }

    uint8_t getBoardNumb() { return boardNumb; }
    void setBoardNumb(uint8_t brd) { boardNumb = brd; }

    uint8_t getChannelNumb() { return chNumb; }
    void setChannelNumb(uint8_t ch) { chNumb = ch; }

    uint8_t getDetectorType() { return detectorType; }
    void setDetectorType(uint8_t tp) { detectorType = tp; }

    uint8_t getDetectorLut() { return detectorLut; }
    void setDetectorLut(uint8_t lut) { detectorLut = lut; }

    uint16_t getRelativeTimeNs() { return relativeTimeNs; }
    void setRelativeTimeNs(uint16_t val) { relativeTimeNs = val; }

    uint16_t getRelativeTimePs() { return relativeTimePs; }
    void setRelativeTimePs(uint16_t val) { relativeTimePs = val; }

    uint8_t getCoincPair() { return varCoincPair; }
    void setCoincPair(uint8_t pair) { varCoincPair = pair; }

    pair<uint8_t, uint8_t> getCoincPairType() { return varCoincPairType; }
    void setCoincPairType(pair<uint8_t, uint8_t> pair) { varCoincPairType = pair; }

    bool getAntiCoincFlag() { return varAntiCoincFlag; }
    void setAntiCoincFlag(bool flag) { varAntiCoincFlag = flag; }

    double getPSD() { return varPSD; }
    void setPSD(double psd) { varPSD = psd; }

private:
    uint64_t timeStamp = 0;

    uint16_t energy = 0;

    uint16_t relativeTimeNs = 0;
    uint16_t relativeTimePs = 0;

    uint8_t detectorType = 0;
    uint8_t detectorLut = 0;
    uint8_t boardNumb = 0;
    uint8_t chNumb = 0;
    uint8_t varCoincPair;

    double varPSD;

    pair<uint8_t, uint8_t> varCoincPairType;

    bool varAntiCoincFlag = false;
};

inline void validateJsonSchema(json configFile, json schemaFile)
{
    json_validator validator(nullptr, nlohmann::json_schema::default_string_format_check, nullptr);

    try
    {
        validator.set_root_schema(schemaFile);
    }
    catch (const std::exception &exp)
    {
        throw "validation of schema failed: " + string(exp.what());
    }

    try
    {
        validator.validate(configFile);
    }
    catch (const std::exception &exp)
    {
        throw "configFile validation of schema failed: " + string(exp.what());
    }
}

inline string getEnvironment(string var)
{
    auto val = std::getenv(var.c_str());
    if (val == nullptr)
    {
        throw "Environment variable " + var + " not set";
    }
    else
    {
        return val;
    }
}

inline uint64_t getCrtTime()
{
    auto now = chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto nanos = chrono::duration_cast<chrono::nanoseconds>(duration).count();

    return uint64_t(nanos);
}
#endif