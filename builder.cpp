#include <iostream>
#include "TFile.h"
#include "json.hpp"
#include <fstream>
#include "TChain.h"
#include <deque>
#include "utils.h"
#include <math.h>
#include <iomanip>
#include "TH1D.h"
#include "TSystem.h"
#include "TApplication.h"
#include "TPad.h"
#include <string>
#include <iostream>
#include <filesystem>
#include <future>
#include "TROOT.h"
#include <list>
#include <parallel/algorithm>
#include "dataWriter.h"
#include "TH2D.h"
#include "TF1.h"

#include <variant>
#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <fcntl.h>
#include <limits>

using json = nlohmann::json;
using namespace std;

deque<event *> convertRoot(json &config);
deque<event *> convertProto(json &config);
deque<event *> convertDelila(json &config);
deque<event *> convert3MV(json &config);
deque<event *> convertCap(json &config);

vector<tuple<int, int, uint64_t>> convertRootDelayEvents(json &config);
vector<tuple<int, int, uint64_t>> convertProtoDelayEvents(json &config);
vector<tuple<int, int, uint64_t>> convertDelilaDelayEvents(json &config);
vector<tuple<int, int, uint64_t>> convert3MVDelayEvents(json &config);
vector<tuple<int, int, uint64_t>> convertCapDelayEvents(json &config);

void builder(json &config, deque<event *> varEventPool, vector<vector<int>> coincMask);
void addToCheckMask(vector<int> &checkMask, event &evt);
void preProcessEvent(json &config, deque<event *> &events);
void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void buildRun(json config);
void delayRun(json config);
bool validateEvent(vector<event *> *events);

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask);
bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask);

pair<int, int> findBoardChannel(json &config, int detType, int lut);
pair<int, int> findCoincPairType(json &config, int brd, int ch);

string extractNumbers(const string &a, string format = "proto");

pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events);

double getTimeStd(pair<deque<event *>::iterator, deque<event *>::iterator> evt);
dataWriter *writer;

int main(int argc, char **argv)
{

    if (argc > 1)
    {

        json request = json::parse(argv[1]);

        if (request["crtRun"] == "build")
        {
            buildRun(request);
        }
        else if (request["crtRun"] == "delay")
        {
            delayRun(request);
        }
    }
}

void buildRun(json config)
{

    string dataType = config["input"]["fileFormat"];

    deque<event *> events;

    if (dataType == "root")
    {
        events = convertRoot(config);
    }
    else if (dataType == "proto")
    {
        events = convertProto(config);
    }
    else if (dataType == "delila")
    {
        events = convertDelila(config);
    }
    else if (dataType == "3MV")
    {
        events = convert3MV(config);
    }
    else if (dataType == "cap")
    {
        events = convertCap(config);
    }

    vector<vector<int>> coincMask = config["build"]["coincMask"].get<vector<vector<int>>>();

    builder(config, events, coincMask);
}

pair<int, int> findBoardChannel(json &config, int detType, int lut)
{

    vector<vector<int>> lutVect = config["detector"]["lut"];
    vector<vector<int>> bTypeVect = config["detector"]["type"];

    for (int i = 0; i < lutVect.size(); i++)
    {
        for (int o = 0; o < lutVect[0].size(); o++)
        {
            if (lutVect[i][o] == lut && bTypeVect[i][o] == detType)
            {
                return make_pair(i, o);
            }
        }
    }

    cout << "Board and channel not found for detType: " << detType << " and lut: " << lut << endl;

    abort();

    return {-1, -1};
}
vector<vector<int>> getDelayVector(json delay)
{

    vector<vector<int>> tempDelay = delay.get<vector<vector<int>>>();

    for (auto &board : tempDelay)
    {
        for (auto &ch : board)
        {
            ch = ch * 1000;
        }
    }

    return tempDelay;
}

deque<event *> convertProto(json &config)
{

    deque<event *> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    string fileName = config["crtFile"];

    auto fd = open(fileName.c_str(), O_RDONLY);

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPair = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    map<pair<int, int>, pair<int, int>> coincPairTypes;
    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    kj::FdInputStream fdStream(fd);
    kj::BufferedInputStreamWrapper bufferedStream(fdStream);

    while (bufferedStream.tryGetReadBuffer() != nullptr)
    {
        capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

        auto dts = message.getRoot<Dataset>();

        auto proEvent = dts.getEvents();

        for (uint i = 0; i < proEvent.size(); i++)
        {

            auto ev = new event();

            auto brd = proEvent[i].getBoard();
            auto ch = proEvent[i].getChannel();

            ev->setBoardNumb(brd);
            ev->setChannelNumb(ch);
            ev->setLutNumb(lut[brd][ch]);
            ev->setDetectorType(mask[brd][ch]);

            ev->setEnergy(proEvent[i].getEnergy());
            ev->setPSD(proEvent[i].getPsd() * 10000);

            uint64_t delayedTime = proEvent[i].getTimestamp() - delay[brd][ch];
            ev->setTimeStamp(delayedTime);

            // coinc
            ev->setCoincPair(coincPair[brd][ch]);

            auto findPair = make_pair(brd, ch);

            if (!coincPairTypes.count(findPair))
            {
                coincPairTypes[findPair] = findCoincPairType(config, brd, ch);
            }

            ev->setCoincPairType(coincPairTypes[findPair]);

            if (find(antiCoincFlags.begin(), antiCoincFlags.end(), ev->getDetectorType()) != antiCoincFlags.end())
            {
                ev->setAntiCoincFlag(true);
            }

            events.push_back(ev);

            if (events.size() == eventNumb)
            {
                return events;
            }
        }
    }

    return events;
}

deque<event *> convertCap(json &config)
{

    deque<event *> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    string fileName = config["crtFile"];

    auto fd = open(fileName.c_str(), O_RDONLY);

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPair = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    map<pair<int, int>, pair<int, int>> coincPairTypes;
    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    kj::FdInputStream fdStream(fd);
    kj::BufferedInputStreamWrapper bufferedStream(fdStream);

    while (bufferedStream.tryGetReadBuffer() != nullptr)
    {
        capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

        variant<PlainData::Reader, PsdData::Reader> dts = message.getRoot<PlainData>();

        int evtType = get<PlainData::Reader>(dts).getType();

        switch (evtType)
        {
        case 0:
            break;
        case 1:
            dts = message.getRoot<PsdData>();
            break;
        default:
            cout << "Unsupported event type" << endl;
            abort();
            break;
        }

        auto done = visit([&](auto &data) -> bool
                          {

                        auto proEvent = data.getEvents();

                        for (uint i = 0; i < proEvent.size(); i++){

                                    auto ev = new event();

                                    auto brd = proEvent[i].getBoard();
                                    auto ch = proEvent[i].getChannel();

                                    ev->setBoardNumb(brd);
                                    ev->setChannelNumb(ch);
                                    ev->setLutNumb(lut[brd][ch]);
                                    ev->setDetectorType(mask[brd][ch]);

                                    ev->setEnergy(proEvent[i].getEnergy());

                                    using T = std::decay_t<decltype(proEvent[i])>;
                                    if constexpr (std::is_same_v<T, PsdEvent>){
                                        ev->setPSD(proEvent[i].getPsd() * 10000);
                                    } else {
                                        ev->setPSD(0);
                                    }


                                    uint64_t delayedTime = proEvent[i].getTimestamp() - delay[brd][ch];
                                    ev->setTimeStamp(delayedTime);

                                    // coinc
                                    ev->setCoincPair(coincPair[brd][ch]);

                                    auto findPair = make_pair(brd, ch);

                                    if (!coincPairTypes.count(findPair))
                                    {
                                        coincPairTypes[findPair] = findCoincPairType(config, brd, ch);
                                    }

                                    ev->setCoincPairType(coincPairTypes[findPair]);

                                    if (find(antiCoincFlags.begin(), antiCoincFlags.end(), ev->getDetectorType()) != antiCoincFlags.end())
                                    {
                                        ev->setAntiCoincFlag(true);
                                    }

                                    events.push_back(ev);

                                    if (events.size() == eventNumb)
                                    {
                                        return true;
                                    }
                                }
                                
                      return false; },
                          dts);

        if (done)
        {
            return events;
        }
    }

    return events;
}

deque<event *> convertDelila(json &config)
{

    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);

    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    map<pair<int, int>, pair<int, int>> coincPairTypes;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("ELIADE_Tree");

    UChar_t Mod;
    UChar_t Ch;
    uint16_t energy;
    ULong64_t timeStamp;
    double fineTs;

    chain->SetBranchAddress("Mod", &Mod);
    chain->SetBranchAddress("Ch", &Ch);
    chain->SetBranchAddress("ChargeLong", &energy);
    chain->SetBranchAddress("TimeStamp", &timeStamp);
    chain->SetBranchAddress("FineTS", &fineTs);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        int board = Mod;
        int ch = Ch;

        auto lut = config["detector"]["lut"][board][ch];
        auto dType = config["detector"]["type"][board][ch];

        uint64_t delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event(dType, energy, delayedTime, board, ch));
        events.back()->setLutNumb(lut);
        events.back()->setCoincPair(coincPairs[board][ch]);

        auto findPair = make_pair(board, ch);

        if (!coincPairTypes.count(findPair))
        {
            coincPairTypes[findPair] = findCoincPairType(config, board, ch);
        }

        events.back()->setCoincPairType(coincPairTypes[findPair]);

        if (find(antiCoincFlags.begin(), antiCoincFlags.end(), events.back()->getDetectorType()) != antiCoincFlags.end())
        {
            events.back()->setAntiCoincFlag(true);
        }
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

deque<event *> convert3MV(json &config)
{

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();

    uint16_t ch;
    uint16_t energy;
    ULong64_t timeStamp;
    double psd;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("events");

    chain->SetBranchAddress("energy", &energy);
    chain->SetBranchAddress("boardTimeStamp", &timeStamp);
    chain->SetBranchAddress("detectorId", &ch);
    chain->SetBranchAddress("psd", &psd);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int board = 0;
    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        auto delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event(mask[board][ch], energy, delayedTime, board, ch));
        events.back()->setLutNumb(lut[board][ch]);

        events.back()->setPSD(psd);
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

deque<event *> convertRoot(json &config)
{

    vector<vector<int>> boards = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> channels = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);

    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    map<pair<int, int>, pair<int, int>> coincPairTypes;

    uint16_t lut;
    uint16_t dType;
    uint16_t energy;
    ULong64_t timeStamp;
    double psd;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("events");

    chain->SetBranchAddress("energy", &energy);
    chain->SetBranchAddress("boardTimeStamp", &timeStamp);
    chain->SetBranchAddress("detectorLUT", &lut);
    chain->SetBranchAddress("detectorType", &dType);
    chain->SetBranchAddress("psd", &psd);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        auto board = boards[dType][lut];
        auto ch = channels[dType][lut];

        if (board == -100)
        {

            auto boardChannel = findBoardChannel(config, dType, lut);

            board = boards[dType][lut] = get<0>(boardChannel);
            ch = channels[dType][lut] = get<1>(boardChannel);
        }

        auto delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event(dType, energy, delayedTime, board, ch));
        events.back()->setLutNumb(lut);
        events.back()->setCoincPair(coincPairs[board][ch]);

        events.back()->setPSD(psd * 10000);

        auto findPair = make_pair(board, ch);

        if (!coincPairTypes.count(findPair))
        {
            coincPairTypes[findPair] = findCoincPairType(config, board, ch);
        }

        events.back()->setCoincPairType(coincPairTypes[findPair]);

        if (find(antiCoincFlags.begin(), antiCoincFlags.end(), events.back()->getDetectorType()) != antiCoincFlags.end())
        {
            events.back()->setAntiCoincFlag(true);
        }
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

void preProcessEvent(json &config, deque<event *> &events)
{

    if (bool(config["build"]["sortBeforeBuild"]))
    {
        __gnu_parallel::sort(events.begin(), events.end(),
                             [](event *i, event *j)
                             { return (i->getTimeStamp() < j->getTimeStamp()); });
    }
}

void builder(json &config, deque<event *> varEventPool, vector<vector<int>> coincMask)
{

    preProcessEvent(config, varEventPool);

    string fileName = config["crtFile"];

    auto runNumber = stoi(extractNumbers(fileName, config["input"]["fileFormat"]));

    auto pos = fileName.find_last_of("/") + 1;
    auto nSize = 6;

    fileName = fileName.substr(pos, fileName.size());

    pos = fileName.find_last_of(".");

    fileName = fileName.substr(0, pos);

    fileName = string(config["output"]["outputPath"]) + "/" + fileName;

    if (config["output"]["fileFormat"] == "gasp")
    {
        writer = new gaspDataWriter(fileName, runNumber);
    }
    else if (config["output"]["fileFormat"] == "root")
    {
        writer = new rootDataWriter(fileName, runNumber);
    }

    writer->setConfig(config);

    uint64_t coincWindow = config["build"]["coincWindow"];

    vector<pair<deque<event *>::iterator, deque<event *>::iterator>> eventPos;

    for (auto i = varEventPool.begin(); i != varEventPool.end(); i++)
    {
        auto crtPos = i;

        auto interval = (*crtPos)->getTimeStamp() - (*i)->getTimeStamp();
        vector<int> checkMask(coincMask[0].size(), 0);

        while (interval < coincWindow)
        {
            addToCheckMask(checkMask, *(*crtPos));

            crtPos++;

            if (crtPos == varEventPool.end())
            {
                break;
            }

            interval = (*crtPos)->getTimeStamp() - (*i)->getTimeStamp();
        }

        if (validateCheckMask(coincMask, checkMask))
        {
            eventPos.push_back(make_pair(i, crtPos));
        }
    }

    if (eventPos.size())
    {
        for (int i = 0; i < eventPos.size() - 1; i++)
        {
            int cnt = 1;

            auto d1 = distance(eventPos[i].first, eventPos[i].second);
            auto d2 = distance(eventPos[i].first, eventPos[i + cnt].first);

            vector<pair<deque<event *>::iterator, deque<event *>::iterator>> overlapEvents;
            overlapEvents.push_back(eventPos[i]);

            while (d2 < d1)
            {
                overlapEvents.push_back(eventPos[i + cnt]);

                cnt++;

                if (i + cnt >= eventPos.size())
                {
                    break;
                }
                d2 = distance(eventPos[i].first, eventPos[i + cnt].first);
            }

            i += overlapEvents.size() - 1;

            auto evt = selectOverlapEvent(overlapEvents);

            postProcessEvent(config, evt);
        }
    }

    for (auto i : varEventPool)
    {
        delete i;
    }

    writer->closeFile();

    gROOT->Reset();
}

pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events)
{

    if (events.size() == 1)
    {
        return events[0];
    }

    vector<int> evtSize;
    int maxValue = 0;

    for (auto evt : events)
    {
        evtSize.push_back(distance(evt.first, evt.second));

        if (evtSize.back() > maxValue)
        {
            maxValue = evtSize.back();
        }
    }

    map<int, double> stdValues;

    for (int i = 0; i < evtSize.size(); i++)
    {
        if (evtSize[i] == maxValue)
        {
            stdValues[i] = getTimeStd(events[i]);
        }
    }

    auto minVal = *min_element(stdValues.begin(), stdValues.end(), [](auto i, auto j)
                               { return i.second < j.second; });

    return events[minVal.first];
}

double getTimeStd(pair<deque<event *>::iterator, deque<event *>::iterator> evt)
{

    auto size = distance(evt.first, evt.second);

    uint64_t sum = accumulate(evt.first, evt.second, uint64_t(0), [](auto sum, auto val)
                              { return sum + val->getTimeStamp(); });

    double mean = sum / size;

    vector<double> diff(size);

    transform(evt.first, evt.second, diff.begin(), [mean](auto x)
              { return double(x->getTimeStamp()) - mean; });

    double sq_sum = inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
    double stdev = sqrt(sq_sum / size);

    return stdev;
}

void addToCheckMask(vector<int> &checkMask, event &evt)
{
    if (evt.getDetectorType() < checkMask.size())
    {
        checkMask.at(evt.getDetectorType())++;
    }
}

bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask)
{

    for (uint i = 0; i < coincMask.size(); i++)
    {
        if (checkMask.at(i) < coincMask.at(i))
        {
            return false;
        }
    }
    return true;
}

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask)
{

    for (uint i = 0; i < coincMask.size(); i++)
    {
        if (validateSingleMask(coincMask.at(i), checkMask))
        {
            return true;
        }
    }

    return false;
}

string extractNumbers(const string &a, string format)
{

    string numberString;

    if (format == "proto" || format == "root" || format == "cap")
    {
        auto pos = a.find_last_of(".");
        auto nSize = 6;

        numberString = a.substr(pos - nSize, nSize);
    }
    else if (format == "delila")
    {
        // this wil most likely not work

        auto fPos = a.find_first_of("_") + 1;
        auto lPos = a.find_last_of("_");

        numberString = a.substr(fPos, lPos - fPos);
    }
    else if (format == "3MV")
    {
        auto fPos = a.find_last_of("_") + 1;
        auto lPos = a.find_last_of(".");

        numberString = a.substr(fPos, lPos - fPos);
    }

    return numberString;
}

void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt)
{

    vector<event *> tempList;

    int64_t absTime = (*evt.first)->getTimeStamp();

    // capture the list
    for (auto i = evt.first; i < evt.second; i++)
    {
        tempList.push_back(new event(*(*i)));
    }

    // sort by (type and timestamp)
    auto start = tempList.begin();

    for (auto val : config["build"]["typePriority"])
    {
        auto it = std::partition(start, tempList.end(), [val](event *ev)
                                 { return ev->getDetectorType() == val; });

        sort(start, it, [](event *i, event *m)
             { return i->getTimeStamp() < m->getTimeStamp(); });

        start = it;
    }

    // set relative times
    for (int i = 0; i < tempList.size(); i++)
    {
        double relativeTime = int64_t(tempList[i]->getTimeStamp()) - int64_t(tempList.front()->getTimeStamp()) + int(config["build"]["coincWindow"]);

        uint16_t timeNs = int(relativeTime) / 1000;
        uint16_t timePs = int(relativeTime) % 1000;

        tempList[i]->setRelativeTimeNs(timeNs);
        tempList[i]->setRelativeTimePs(timePs);
    }

    if (tempList.size() != 1)
    {
        std::sort(tempList.begin(), tempList.end(), [](event *i, event *j)
                  { return (i->getCoincPair() < j->getCoincPair()); });
    }

    // rewrite this ... its kind of shit
    for (uint i = 0; i < tempList.size(); i++)
    {
        if (tempList[i] == nullptr)
        {
            continue;
        }
        else if (i == (tempList.size() - 1))
        {

            if (tempList[i]->getAntiCoincFlag())
            {
                auto pairDetectorType = tempList[i]->getCoincPairType();

                tempList[i]->setVetoEnergy(tempList[i]->getEnergy());
                tempList[i]->setVetoRelativeTimeNs(tempList[i]->getRelativeTimeNs());
                tempList[i]->setVetoRelativeTimePs(tempList[i]->getRelativeTimePs());
                tempList[i]->setDetectorType(pairDetectorType.first);
                if (pairDetectorType.second != -100)
                {
                    tempList[i]->setLutNumb(pairDetectorType.second);
                }
                tempList[i]->setEnergy(0);
                tempList[i]->setPSD(0);
                tempList[i]->setRelativeTimeNs(0);
                tempList[i]->setRelativeTimePs(0);
            }
        }
        else
        {
            if (tempList[i]->getCoincPair())
            {
                if (tempList[i]->getCoincPair() == tempList[i + 1]->getCoincPair())
                {
                    int evt = i;
                    int veto = i + 1;

                    if (tempList[evt]->getAntiCoincFlag())
                    {
                        evt = i + 1;
                        veto = i;
                    }

                    tempList[evt]->setVetoEnergy(tempList[veto]->getEnergy());
                    tempList[evt]->setVetoRelativeTimeNs(tempList[veto]->getRelativeTimeNs());
                    tempList[evt]->setVetoRelativeTimePs(tempList[veto]->getRelativeTimePs());

                    delete tempList[veto];
                    tempList[veto] = nullptr;
                }
                else
                {
                    if (tempList[i]->getAntiCoincFlag())
                    {
                        auto pairDetectorType = tempList[i]->getCoincPairType();

                        tempList[i]->setVetoEnergy(tempList[i]->getEnergy());
                        tempList[i]->setVetoRelativeTimeNs(tempList[i]->getRelativeTimeNs());
                        tempList[i]->setVetoRelativeTimePs(tempList[i]->getRelativeTimePs());
                        tempList[i]->setDetectorType(pairDetectorType.first);
                        if (pairDetectorType.second != -100)
                        {
                            tempList[i]->setLutNumb(pairDetectorType.second);
                        }
                        tempList[i]->setEnergy(0);
                        tempList[i]->setPSD(0);
                        tempList[i]->setRelativeTimeNs(0);
                        tempList[i]->setRelativeTimePs(0);
                    }
                }
            }
        }
    }
    tempList.erase(remove(tempList.begin(), tempList.end(), nullptr), tempList.end());
    // rewrite this ... its kind of shit

    std::sort(tempList.begin(), tempList.end(), [](event *i, event *j)
              { return (i->getDetectorType() < j->getDetectorType()); });

    // we could validate sooner ... but maybe there are other validations to be added
    if (validateEvent(&tempList))
    {
        writer->addBuiltEvent(&tempList, absTime);
    }

    for (int i = 0; i < tempList.size(); i++)
    {
        delete tempList[i];
    }
}

bool validateEvent(vector<event *> *events)
{

    // remove events with duplicate signals
    map<pair<int, int>, int> counts;

    bool found0 = false;
    bool found3 = false;

    for (auto &ev : *events)
    {
        counts[make_pair(ev->getLutNumb(), ev->getDetectorType())]++;
    }

    if (counts.size() != events->size())
    {
        return false;
    }

    return true;
}

pair<int, int> findCoincPairType(json &config, int brd, int ch)
{

    int foundBrd = 0;

    int pair = config["detector"]["coincPairs"][brd][ch];

    for (auto i : config["detector"]["coincPairs"])
    {
        auto pos = find(i.begin(), i.end(), pair);

        if (pos != i.end())
        {

            if (brd != foundBrd || ch != distance(i.begin(), pos))
            {

                auto p = make_pair(config["detector"]["type"][foundBrd][distance(i.begin(), pos)],
                                   config["detector"]["lut"][foundBrd][distance(i.begin(), pos)]);

                return p;
            }
        }

        foundBrd += 1;
    }

    return make_pair(0, -100);
}

vector<tuple<int, int, uint64_t>> convertRootDelayEvents(json &config)
{

    vector<tuple<int, int, uint64_t>> events;

    auto files = config["crtFile"].get<vector<string>>();

    TChain chain("events");

    for (auto file : files)
    {
        chain.Add(file.c_str());
    }

    uint16_t dLut;
    uint16_t dType;
    uint16_t energy;
    ULong64_t timeStamp;

    chain.SetBranchAddress("energy", &energy);
    chain.SetBranchAddress("boardTimeStamp", &timeStamp);
    chain.SetBranchAddress("detectorLUT", &dLut);
    chain.SetBranchAddress("detectorType", &dType);

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain.GetEntries();
    }

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (int i = 0; i < eventNumb; i++)
    {
        chain.GetEntry(i);

        if (energy > tresh)
        {

            if (dType == refType && dLut == refLUT)
            {
                timeStamp += offset;
            }

            events.push_back(make_tuple(dType, dLut, timeStamp));
        }
    }

    return events;
}

vector<tuple<int, int, uint64_t>> convert3MVDelayEvents(json &config)
{

    vector<tuple<int, int, uint64_t>> events;

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    TChain chain("events");

    for (auto file : files)
    {
        chain.Add(file.c_str());
    }

    uint16_t ch;
    uint16_t energy;
    ULong64_t timeStamp;

    chain.SetBranchAddress("energy", &energy);
    chain.SetBranchAddress("boardTimeStamp", &timeStamp);
    chain.SetBranchAddress("detectorId", &ch);

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain.GetEntries();
    }

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (int i = 0; i < eventNumb; i++)
    {
        chain.GetEntry(i);

        if (energy > tresh)
        {

            auto brd = 0;

            auto dType = mask[brd][ch];
            auto dLut = lut[brd][ch];

            if (dType == refType && dLut == refLUT)
            {
                timeStamp += offset;
            }

            events.push_back(make_tuple(dType, dLut, timeStamp));
        }
    }

    return events;
}

vector<tuple<int, int, uint64_t>> convertProtoDelayEvents(json &config)
{
    vector<tuple<int, int, uint64_t>> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (auto fileName : files)
    {

        auto fd = open(fileName.c_str(), O_RDONLY);

        kj::FdInputStream fdStream(fd);
        kj::BufferedInputStreamWrapper bufferedStream(fdStream);

        while (bufferedStream.tryGetReadBuffer() != nullptr)
        {
            capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

            auto dts = message.getRoot<Dataset>();

            auto proEvent = dts.getEvents();

            for (uint i = 0; i < proEvent.size(); i++)
            {

                auto brd = proEvent[i].getBoard();
                auto ch = proEvent[i].getChannel();

                auto dType = mask[brd][ch];
                auto dLut = lut[brd][ch];

                if (proEvent[i].getEnergy() > tresh)
                {

                    auto timeStamp = proEvent[i].getTimestamp();

                    if (dType == refType && dLut == refLUT)
                    {
                        timeStamp += offset;
                    }

                    events.push_back(make_tuple(dType, dLut, timeStamp));

                    if (events.size() == eventNumb)
                    {
                        return events;
                    }
                }
            }
        }
    }

    return events;
}

template <class... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

vector<tuple<int, int, uint64_t>> convertCapDelayEvents(json &config)
{
    vector<tuple<int, int, uint64_t>> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (auto fileName : files)
    {

        auto fd = open(fileName.c_str(), O_RDONLY);

        kj::FdInputStream fdStream(fd);
        kj::BufferedInputStreamWrapper bufferedStream(fdStream);

        while (bufferedStream.tryGetReadBuffer() != nullptr)
        {
            capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

            variant<PlainData::Reader, PsdData::Reader> dts = message.getRoot<PlainData>();

            int evtType;

            visit([&](auto &data)
                  { 
                    
                   switch(data.getType()){
                    case 0:
                        break;

                    case 1:
                        dts = message.getRoot<PsdData>();
                        break;

                    default:
                        cout << "cap data type not supported by builder" << endl;
                        abort();

                   } },

                  dts);

            auto done = visit([&](auto &data) -> bool
                              {
                      auto proEvent = data.getEvents();

                      for (uint i = 0; i < proEvent.size(); i++)
                      {

                          auto brd = proEvent[i].getBoard();
                          auto ch = proEvent[i].getChannel();

                          auto dType = mask[brd][ch];
                          auto dLut = lut[brd][ch];

                          if (proEvent[i].getEnergy() > tresh)
                          {

                              auto timeStamp = proEvent[i].getTimestamp();

                              if (dType == refType && dLut == refLUT)
                              {
                                  timeStamp += offset;
                              }
                
                              events.push_back(make_tuple(dType, dLut, timeStamp));

                              if (events.size() == eventNumb)
                              {
                                  return true;
                              }
                          }
                      }
                      
                      return false; },
                              dts);

            if (done)
            {
                return events;
            }
        }
    }
    return events;
}

vector<tuple<int, int, uint64_t>> convertDelilaDelayEvents(json &config)
{
    cout << "delila delay calculation not yet implemented" << endl;
    abort();
    return {};
}

void delayRun(json config)
{
    string dataType = config["input"]["fileFormat"];

    vector<tuple<int, int, uint64_t>> events;

    if (dataType == "root")
    {
        events = convertRootDelayEvents(config);
    }
    else if (dataType == "proto")
    {
        events = convertProtoDelayEvents(config);
    }
    else if (dataType == "cap")
    {
        events = convertCapDelayEvents(config);
    }
    else if (dataType == "delila")
    {
        events = convertDelilaDelayEvents(config);
    }
    else if (dataType == "3MV")
    {
        events = convert3MVDelayEvents(config);
    }
    else
    {
        cout << "unknown data type" << endl;
        abort();
    }

    sort(events.begin(), events.end(),
         [](auto i, auto j)
         { return (get<2>(i) < get<2>(j)); });

    double refTimeStamp = 0;

    map<pair<int, int>, shared_ptr<TH1D>> hists;

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];

    for (auto evt : events)
    {

        if (get<0>(evt) == refType && get<1>(evt) == refLUT)
        {
            refTimeStamp = get<2>(evt);
        }

        double timeDif = double(get<2>(evt)) - refTimeStamp;

        auto id = make_pair(get<0>(evt), get<1>(evt));

        if (hists.count(id))
        {
            hists[id]->Fill(timeDif);
        }
        else
        {
            string name = "type_" + to_string(get<0>(evt)) + "_lut_" + to_string(get<1>(evt));

            int bins = config["delay"]["histBins"];
            auto range = config["delay"]["histRange"].get<vector<double>>();

            if (range.size() != 2)
            {
                range = {0, 1000000};
            }

            hists[id] = make_shared<TH1D>(TH1D(name.c_str(), name.c_str(), bins, range[0], range[1]));
        }
    }

    vector<vector<int>> delay = config["detector"]["delayNs"];

    for (auto hist : hists)
    {
        if (hist.second->GetEntries())
        {
            int binmax = hist.second->GetMaximumBin();
            double x = hist.second->GetXaxis()->GetBinCenter(binmax);

            auto brdCh = findBoardChannel(config, hist.first.first, hist.first.second);

            auto time = x;

            if (hist.first.first == refType && hist.first.second == refLUT)
            {
                time = 0;
            }
            else
            {
                time += int(config["delay"]["refTimeOffset"]);
            }

            delay[brdCh.first][brdCh.second] = time / 1000;
        }
    }

    config["detector"]["delayNs"] = delay;

    config.erase("crtFile");
    config.erase("crtRun");

    std::ofstream outConfigFile("config.json");
    outConfigFile << std::setw(2) << config << std::endl;

    string outFileName = config["delay"]["outFileName"];

    auto outFile = TFile(outFileName.c_str(), "RECREATE");

    for (auto &hist : hists)
    {
        hist.second->Write();
    }

    outFile.Close();
}
