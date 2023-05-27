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

    uint16_t getDetectorType() { return detectorType; }
    void setDetectorType(uint16_t tp) { detectorType = tp; }

    uint16_t getEnergy() { return energy; }
    void setEnergy(uint16_t nrg) { energy = nrg; }

    uint16_t getVetoEnergy() { return vetoEnergy; }
    void setVetoEnergy(uint16_t nrg) { vetoEnergy = nrg; }

    uint16_t getBoardNumb() { return boardNumb; }
    void setBoardNumb(uint16_t brd) { boardNumb = brd; }

    uint16_t getChannelNumb() { return chNumb; }
    void setChannelNumb(uint16_t ch) { chNumb = ch; }

    uint16_t getLutNumb() { return lutNumb; }
    void setLutNumb(uint16_t lut) { lutNumb = lut; }

    uint16_t getRelativeTimeNs() { return relativeTimeNs; }
    void setRelativeTimeNs(uint16_t val) { relativeTimeNs = val; }

    uint16_t getRelativeTimePs() { return relativeTimePs; }
    void setRelativeTimePs(uint16_t val) { relativeTimePs = val; }

    uint16_t getVetoRelativeTimeNs() { return vetoRelativeTimeNs; }
    void setVetoRelativeTimeNs(uint16_t val) { vetoRelativeTimeNs = val; }

    uint16_t getVetoRelativeTimePs() { return vetoRelativeTimePs; }
    void setVetoRelativeTimePs(uint16_t val) { vetoRelativeTimePs = val; }

    int getCoincPair() { return varCoincPair; }
    void setCoincPair(int pair) { varCoincPair = pair; }

    pair<int, int> getCoincPairType() { return varCoincPairType; }
    void setCoincPairType(pair<int, int> pair) { varCoincPairType = pair; }

    bool getAntiCoincFlag() { return varAntiCoincFlag; }
    void setAntiCoincFlag(bool flag) { varAntiCoincFlag = flag; }

    uint16_t getPSD() { return varPSD; }
    void setPSD(uint16_t psd) { varPSD = psd; }

    uint64_t getRawTs() { return rawTs; }
    void setRawTs(uint64_t ts) { rawTs = ts; }

    uint16_t getRawFineTs() { return rawFineTs; }
    void setRawFineTs(uint16_t ts) { rawFineTs = ts; }

private:
    uint64_t rawTs = 0;
    uint16_t rawFineTs = 0;

    uint64_t timeStamp = 0;

    uint16_t energy = 0;
    uint16_t vetoEnergy = 0;

    uint16_t detectorType = 0;
    uint16_t lutNumb = 0;

    uint16_t boardNumb = 0;
    uint16_t chNumb = 0;

    uint16_t relativeTimeNs = 0;
    uint16_t relativeTimePs = 0;

    uint16_t vetoRelativeTimeNs = 0;
    uint16_t vetoRelativeTimePs = 0;

    int varCoincPair;
    uint16_t varPSD;

    pair<int, int> varCoincPairType;

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

#endif