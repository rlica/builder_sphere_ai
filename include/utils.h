#pragma once

#include <chrono>
#include <iostream>
#include <map>
#include "json-schema.hpp"
#include "json.hpp"

using namespace std;
using nlohmann::json_uri;
using nlohmann::json_schema::json_validator;
using json = nlohmann::json;

typedef unsigned __int128 uint128_t;
typedef __float128 float128_t;

inline map<string, string> fileExtensions = {{"root", ".root"},
                                             {"3MV", ".root"},
                                             {"proto", ".cap"},
                                             {"cap", ".cap"},
                                             {"delila", ".root"},
                                             {"gasp", ""},
                                             {"bell", ".root"},
                                             {"compass", ".root"},
                                             {"compassWave", ".root"},
                                             {"timepix", ".cap"},
                                             {"test", ".cap"}};

class event
{

public:
    event() = default;

    ~event()
    {
        if (waveform)
        {
            delete waveform;
        }
    }

    event(const event &o)
        : timeStamp(o.timeStamp),
          relativeTime(o.relativeTime),
          varPSD(o.varPSD),
          energy(o.energy),
          detectorType(o.detectorType),
          detectorLut(o.detectorLut),
          boardNumb(o.boardNumb),
          chNumb(o.chNumb),
          varCoincPair(o.varCoincPair),
          varAntiCoincFlag(o.varAntiCoincFlag),
          varCoincPairType(o.varCoincPairType)
    {
        if (o.waveform)
        {
            waveform = new vector<int16_t>(*o.waveform);
        }
    }

    event(event &&o) noexcept
        : timeStamp(o.timeStamp),
          relativeTime(o.relativeTime),
          varPSD(o.varPSD),
          waveform(o.waveform),
          energy(o.energy),
          detectorType(o.detectorType),
          detectorLut(o.detectorLut),
          boardNumb(o.boardNumb),
          chNumb(o.chNumb),
          varCoincPair(o.varCoincPair),
          varAntiCoincFlag(o.varAntiCoincFlag),
          varCoincPairType(o.varCoincPairType)
    {
        o.waveform = nullptr;
    }

    event &operator=(const event &o)
    {
        if (this != &o)
        {
            timeStamp = o.timeStamp;
            relativeTime = o.relativeTime;
            varPSD = o.varPSD;
            energy = o.energy;
            detectorType = o.detectorType;
            detectorLut = o.detectorLut;
            boardNumb = o.boardNumb;
            chNumb = o.chNumb;
            varCoincPair = o.varCoincPair;
            varAntiCoincFlag = o.varAntiCoincFlag;
            varCoincPairType = o.varCoincPairType;
            if (waveform)
            {
                delete waveform;
                waveform = nullptr;
            }
            if (o.waveform)
            {
                waveform = new vector<int16_t>(*o.waveform);
            }
        }
        return *this;
    }

    event &operator=(event &&o) noexcept
    {
        if (this != &o)
        {
            timeStamp = o.timeStamp;
            relativeTime = o.relativeTime;
            varPSD = o.varPSD;
            energy = o.energy;
            detectorType = o.detectorType;
            detectorLut = o.detectorLut;
            boardNumb = o.boardNumb;
            chNumb = o.chNumb;
            varCoincPair = o.varCoincPair;
            varAntiCoincFlag = o.varAntiCoincFlag;
            varCoincPairType = o.varCoincPairType;
            if (waveform)
            {
                delete waveform;
            }
            waveform = o.waveform;
            o.waveform = nullptr;
        }
        return *this;
    }

    uint64_t getTimeStamp() const { return timeStamp; }
    void setTimeStamp(uint64_t val) { timeStamp = val; }

    uint16_t getEnergy() const { return energy; }
    void setEnergy(uint16_t nrg) { energy = nrg; }

    uint8_t getBoardNumb() const { return boardNumb; }
    void setBoardNumb(uint8_t brd) { boardNumb = brd; }

    uint16_t getChannelNumb() const { return chNumb; }
    void setChannelNumb(uint16_t ch) { chNumb = ch; }

    uint16_t getDetectorType() const { return detectorType; }
    void setDetectorType(uint16_t tp) { detectorType = tp; }

    uint16_t getDetectorLut() const { return detectorLut; }
    void setDetectorLut(uint16_t lut) { detectorLut = lut; }

    int32_t getRelativeTime() const { return relativeTime; }
    void setRelativeTime(int32_t val) { relativeTime = val; }

    uint8_t getCoincPair() const { return varCoincPair; }
    void setCoincPair(uint8_t pair) { varCoincPair = pair; }

    [[deprecated]] pair<uint8_t, uint8_t> getCoincPairType() const { return varCoincPairType; }
    void setCoincPairType(pair<uint8_t, uint8_t> pair) { varCoincPairType = pair; }

    bool getAntiCoincFlag() const { return varAntiCoincFlag; }
    void setAntiCoincFlag(bool flag) { varAntiCoincFlag = flag; }

    double getPSD() const { return varPSD; }
    void setPSD(double psd) { varPSD = static_cast<float>(psd); }

    void setWaveform(const vector<int16_t> &wave)
    {
        if (waveform)
        {
            delete waveform;
            waveform = nullptr;
        }
        if (!wave.empty())
        {
            waveform = new vector<int16_t>(wave);
        }
    }
    vector<int16_t> getWaveform() const
    {
        if (waveform)
        {
            return *waveform;
        }
        return {};
    }

    void setWaveform(shared_ptr<vector<double>> wave)
    {
        if (waveform)
        {
            delete waveform;
            waveform = nullptr;
        }
        if (wave && !wave->empty())
        {
            waveform = new vector<int16_t>(wave->size());
            for (size_t i = 0; i < wave->size(); i++)
            {
                (*waveform)[i] = (*wave)[i];
            }
        }
    }

private:
    uint64_t timeStamp = 0;
    int32_t relativeTime = 0;
    float varPSD = 0.0f;
    vector<int16_t> *waveform = nullptr;
    uint16_t energy = 0;
    uint8_t detectorType = 0;
    uint8_t detectorLut = 0;
    uint8_t boardNumb = 0;
    uint8_t chNumb = 0;
    uint8_t varCoincPair = 0;
    bool varAntiCoincFlag = false;
    pair<uint8_t, uint8_t> varCoincPairType = {0, 0};
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

class SaxPrinter
{
public:
    SaxPrinter(ostream &oStr) : stream(oStr)
    {
    }
    ~SaxPrinter()
    {
        assert(first_value.empty());
    }

    bool null()
    {
        print_comma();
        stream << "null";
        return true;
    }

    bool boolean(bool value)
    {
        print_comma();
        stream << (value ? "true" : "false");
        return true;
    }

    bool number_integer(json::number_integer_t value)
    {
        print_comma();
        stream << value;
        return true;
    }

    bool number_unsigned(json::number_unsigned_t value)
    {
        print_comma();
        stream << value;
        return true;
    }

    bool number_float(json::number_float_t value, const std::string & /*unused*/)
    {
        print_comma();
        stream << value;
        return true;
    }

    bool string(const std::string &value)
    {
        print_comma();

        stream << "\"" << value << "\"";
        return true;
    }

    bool binary(std::vector<std::uint8_t> & /*unused*/)
    {
        return true;
    }

    bool start_object(std::size_t /*unused*/)
    {
        print_comma();
        if (accSpace)
        {
            stream << " ";
        }
        accSpace = true;

        stream << "{" << endl;

        first_value.push_back(true);
        deepth++;
        return true;
    }

    bool key(const std::string &key)
    {
        print_comma();
        first_value.back() = true; // do not print a comma when printing the value following this key

        for (int i = 0; i < deepth; i++)
        {
            stream << spacer;
        }

        stream << "\"" << key << "\":";
        return true;
    }

    bool end_object()
    {
        deepth--;

        stream << endl;
        for (int i = 0; i < deepth; i++)
        {
            stream << spacer;
        }
        stream << "}";
        first_value.pop_back();
        return true;
    }

    bool start_array(std::size_t /*unused*/)
    {
        print_comma();

        switch (arrayDepth)
        {
        case 0:
            stream << "[";
            break;
        case 1:

            deepth++;
            stream << endl;
            for (int i = 0; i < deepth; i++)
            {
                stream << spacer;
            }

            stream << "[";
            break;
        }

        arrayDepth++;

        if (arrayDepth == 2)
        {
            db = true;
        }

        first_value.push_back(true);
        return true;
    }

    bool end_array()
    {

        switch (arrayDepth)
        {
        case 1:
            if (db)
            {
                stream << endl;
                for (int i = 0; i < deepth; i++)
                {
                    stream << spacer;
                }
                stream << "]";
                db = false;
            }
            else
            {
                stream << "]";
            }

            break;
        case 2:
            stream << "]";
            deepth--;
            break;
        }
        arrayDepth--;

        first_value.pop_back();

        return true;
    }

    bool parse_error(std::size_t /*unused*/, const std::string & /*unused*/, const json::exception & /*unused*/)
    {
        return false;
    }

private:
    std::ostream &stream;

    std::vector<bool> first_value;

    int deepth = 0;
    int arrayDepth = 0;
    bool db = false;

    bool accSpace = false;

    std::string spacer = "  ";

    void print_comma()
    {
        if (!first_value.empty())
        {
            if (first_value.back())
            {
                first_value.back() = false;
            }
            else
            {
                if (arrayDepth)
                {
                    stream << ",";
                }
                else
                {
                    stream << "," << endl;
                }
            }
        }
    }
};
