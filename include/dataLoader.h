#pragma once

#include <iostream>
#include <fcntl.h>
#include <deque>
#include <variant>

#include "json.hpp"
#include "utils.h"
// #include "waveformUtils.h"

#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TChain.h"
#include "TH1D.h"
#include "TPad.h"

#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <filesystem>

using json = nlohmann::json;

namespace fs = std::filesystem;

typedef variant<PlainData::Reader, WaveData::Reader, PsdData::Reader, RawTimeData::Reader> dataReader;
typedef tuple<int, int, uint64_t> delayEvent;
typedef tuple<int, int, uint16_t> alignEvent;

class dataLoader
{
public:
    dataLoader(json cfg) : config(cfg) {};

    virtual deque<event *> readEvents() = 0;
    virtual vector<delayEvent> readDelayEvents() = 0;
    virtual map<pair<uint8_t, uint8_t>, shared_ptr<vector<double>>> readAlignEvents()
    {
        cout << "align not implemented for: " << config["input"]["fileFormat"] << endl;
        return {};
    }

    void setCrtFile(string fileName)
    {
        config["crtFile"] = fileName;
    }

protected:
    json config;
    vector<vector<int>> getDelayVector(json &config);

    pair<uint8_t, uint8_t> findBoardChannel(uint8_t type, uint8_t lut);
    pair<uint8_t, uint8_t> findCoincPairType(uint8_t brd, uint8_t ch);

    map<pair<uint8_t, uint8_t>, pair<uint8_t, uint8_t>> findCoincPairTypes();
};

class rootLoader : public dataLoader
{

public:
    rootLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class protoLoader : public dataLoader
{

public:
    protoLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class delilaLoader : public dataLoader
{

public:
    delilaLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class threeMVLoader : public dataLoader
{

public:
    threeMVLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class capLoader : public dataLoader
{

public:
    capLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();
    map<pair<uint8_t, uint8_t>, shared_ptr<vector<double>>> readAlignEvents();

private:
    map<int, string> dataTypeMap = {{0, "PLAIN_DATA"},
                                    {1, "PSD_DATA"},
                                    {2, "WAVE_DATA"},
                                    {3, "DUAL_WAVE_DATA"},
                                    {4, "FULL_DATA"},
                                    {5, "RAW_TIME_DATA"}};
};

class testLoader : public dataLoader
{

public:
    testLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();
    map<pair<uint8_t, uint8_t>, shared_ptr<vector<double>>> readAlignEvents();

private:
    map<int, string> dataTypeMap = {{0, "FULL_DATA"},
                                    {1, "PLAIN_DATA"},
                                    {2, "PSD_DATA"},
                                    {3, "WAVE_DATA"},
                                    {4, "DUAL_WAVE_DATA"},
                                    {5, "RAW_TIME_DATA"},
                                    {6, "CROSS_DATA"},
                                    {7, "PSD_WAVE_DATA"}};
};

class bellLoader : public dataLoader
{

public:
    bellLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class compassLoader : public dataLoader
{

public:
    compassLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class compassWaveLoader : public dataLoader
{

public:
    compassWaveLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

class timepixLoader : public dataLoader
{

public:
    timepixLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();
    event *parseStringEvent(string &line);

    bool checkTimepixFile();

private:
    deque<event *> readTimepixEvents();
    vector<delayEvent> readTimepixDelayEvents();

    map<int, string> dataTypeMap = {{0, "FULL_DATA"},
                                    {1, "PLAIN_DATA"},
                                    {2, "PSD_DATA"},
                                    {3, "WAVE_DATA"},
                                    {4, "DUAL_WAVE_DATA"},
                                    {5, "RAW_TIME_DATA"},
                                    {6, "CROSS_DATA"},
                                    {7, "PSD_WAVE_DATA"}};
};
