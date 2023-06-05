#ifndef dataloader_h
#define dataloader_h

#include <iostream>
#include <fcntl.h>
#include <deque>
#include <variant>

#include "json.hpp"
#include "utils.h"

#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TChain.h"
#include "TH1D.h"
#include "TPad.h"

#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>

using json = nlohmann::json;

typedef variant<PlainData::Reader, PsdData::Reader, RawTimeData::Reader> dataReader;
typedef tuple<int, int, uint64_t> delayEvent;

class dataLoader
{
public:
    dataLoader(json cfg) : config(cfg){};

    virtual deque<event *> readEvents() = 0;
    virtual vector<delayEvent> readDelayEvents() = 0;

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

private:
    map<int, string> dataTypeMap = {{0, "PLAIN_DATA"},
                                    {1, "PSD_DATA"},
                                    {2, "WAVE_DATA"},
                                    {3, "DUAL_WAVE_DATA"},
                                    {4, "FULL_DATA"},
                                    {5, "RAW_TIME_DATA"}};
};

class bellLoader : public dataLoader
{

public:
    bellLoader(json cfg) : dataLoader(cfg) {}
    deque<event *> readEvents();
    vector<delayEvent> readDelayEvents();

private:
};

#endif