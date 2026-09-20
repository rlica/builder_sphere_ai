#include <iostream>
#include "json.hpp"
#include <fstream>
#include <deque>
#include "utils.h"
#include <math.h>
#include <iomanip>
#include <string>
#include <iostream>
#include <filesystem>
#include <future>
#include <list>
#include <parallel/algorithm>
#include "dataWriter.h"

#include <variant>
#include <limits>
#include "dataLoader.h"
#include <filesystem>
#include <algorithm>
#include <execution>
#include "TApplication.h"
#include <optional>
#include "geneticCalibrator.h"
#include "dataConfig.h"
#include <capnp/ez-rpc.h>
#include <kj/async-io.h>
#include "dataSyncClient.h"
#include <tbb/global_control.h>
#include <tbb/parallel_sort.h>
#include <tbb/parallel_for.h>

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

struct CoincRange
{
    uint32_t start;
    uint32_t end;
};

void addToCheckMask(vector<int> &checkMask, const event &evt);
void preProcessEvent(json &config, vector<event> &events);
void postProcessEvent(json &config, const CoincRange &evt, const vector<event> &eventPool);

void buildRun(json config);
void delayRun(json config);
void convertRun(json config);
void alignRun(json config);

bool validateEvent(const vector<event *> *events);

bool validateCheckMask(const vector<vector<int>> &coincMask, const vector<int> &checkMask);
bool validateSingleMask(const vector<int> &coincMask, const vector<int> &checkMask);

optional<pair<uint8_t, uint8_t>> findBoardChannel(json &config, uint8_t type, uint8_t lut);

string extractNumbers(const string &a, string format = "proto");

CoincRange selectOverlapEvent(const vector<CoincRange> &events, const vector<event> &eventPool);

double getTimeStd(const CoincRange &evt, const vector<event> &eventPool);

void initializeDataLoader(json &config);
void initializeDataWriter(json &config);
void initializeGlobals(json &config);

dataWriter *writer;
shared_ptr<dataLoader> loader;

vector<int> typePriority;
uint32_t coincWindow;
vector<vector<uint8_t>> lutVect;
vector<vector<uint8_t>> bTypeVect;

int main(int argc, char **argv)
{

    TApplication theApp("App", &argc, argv);

    try
    {
        if (argc > 1)
        {
            json config = json::parse(argv[1]);
            auto cfg = dataConfig::getInstance();

            cfg->setConfig(config);
            initializeDataLoader(config);
            initializeDataWriter(config);
            initializeGlobals(config);


            if (config["crtRun"] == "build")
            {
                buildRun(config);
            }
            else if (config["crtRun"] == "delay")
            {
                delayRun(config);
            }
            else if (config["crtRun"] == "convert")
            {
                convertRun(config);
            }
            else if (config["crtRun"] == "align")
            {

                alignRun(config);
            }
            else
            {
                throw "unknown run option, available options: build, delay";
            }
        }
    }
    catch (string &str)
    {
        cout << str << endl;
    }

    auto cfg = dataConfig::getInstance();
    if(cfg->config["crtRun"] == "build"){
        auto rpcClient = dataSyncClient::getInstance();
        rpcClient->updateState({cfg->config["crtId"], "done", 0}, true);
        this_thread::sleep_for(1s);
    }
   
}

void initializeGlobals(json &config)
{
    typePriority = config["build"]["typePriority"].get<vector<int>>();
    coincWindow = config["build"]["coincWindow"].get<uint32_t>() * config["general"]["timeUnitMultiplier"].get<uint32_t>();

    lutVect = config["detector"]["lut"].get<vector<vector<uint8_t>>>();
    bTypeVect = config["detector"]["type"].get<vector<vector<uint8_t>>>();
}

void initializeDataLoader(json &config)
{

    string dataType = config["input"]["fileFormat"];

    if (dataType == "root")
    {
        loader = make_shared<rootLoader>(config);
    }
    else if (dataType == "proto")
    {
        loader = make_shared<protoLoader>(config);
    }
    else if (dataType == "delila")
    {
        loader = make_shared<delilaLoader>(config);
    }
    else if (dataType == "3MV")
    {
        loader = make_shared<threeMVLoader>(config);
    }
    else if (dataType == "cap")
    {
        loader = make_shared<capLoader>(config);
    }
    else if (dataType == "bell")
    {
        loader = make_shared<bellLoader>(config);
    }
    else if (dataType == "compass")
    {
        loader = make_shared<compassLoader>(config);
    }
    else if (dataType == "compassWave")
    {
        loader = make_shared<compassWaveLoader>(config);
    }
    else if (dataType == "test")
    {
        loader = make_shared<testLoader>(config);
    }
    else if (dataType == "timepix")
    {
        loader = make_shared<timepixLoader>(config);
    }
    else
    {
        throw invalid_argument("Invalid data type");
    }
}

void initializeDataWriter(json &config)
{

    auto runType = config["general"]["run"].get<string>();

    if (runType == "build" || runType == "convert")
    {
        string fileName = config["crtFile"];

        auto runNumber = stoi(extractNumbers(fileName, config["input"]["fileFormat"]));

        fileName = fs::path(fileName).stem().string();

        fileName = string(config["output"]["path"]) + "/" + fileName;

        config["run"] = {};
        config["run"]["fileName"] = fileName;
        config["run"]["runNumber"] = runNumber;
    }

    if (config["output"]["fileFormat"] == "gasp")
    {
        writer = new gaspDataWriter(config);
    }
    else if (config["output"]["fileFormat"] == "root")
    {
        writer = new rootDataWriter(config);
    }
    else if (config["output"]["fileFormat"] == "cap")
    {
        writer = new capDataWriter(config);
    }
    else
    {
        throw "Invalid output file format: " + config["output"]["fileFormat"].get<string>();
    }
}

optional<pair<uint8_t, uint8_t>> findBoardChannel(json &config, uint8_t type, uint8_t lut)
{

    for (int i = 0; i < lutVect.size(); i++)
    {
        for (int o = 0; o < lutVect[0].size(); o++)
        {
            if (lutVect[i][o] == lut && bTypeVect[i][o] == type)
            {
                return make_pair(i, o);
            }
        }
    }

    return {};
}

void preProcessEvent(json &config, vector<event> &events)
{
    if (bool(config["build"]["sortBeforeBuild"]))
    {
        auto rpcClient = dataSyncClient::getInstance();

        rpcClient->updateState({config["crtId"], "sorting data (2/4)", 0}, true);

        tbb::parallel_sort(events.begin(), events.end(),
                           [](const event &i, const event &j)
                           {
                               return (i.getTimeStamp() < j.getTimeStamp());
                           });

        rpcClient->updateState({config["crtId"], "sorting data (2/4)", 100}, true);
    }
}

void buildRun(json config)
{
    auto rpcClient = dataSyncClient::getInstance();

    uint32_t coincWindow = uint32_t(config["build"]["coincWindow"]) * int(config["general"]["timeUnitMultiplier"]);
    vector<vector<int>> coincMask = config["build"]["coincMask"].get<vector<vector<int>>>();

    bool streamed = loader->openStream();
    if (!streamed)
    {
        // Fallback for non-streaming loaders
        vector<event> varEventPool = loader->readEventsFlat();
        varEventPool.shrink_to_fit();
        preProcessEvent(config, varEventPool);

        size_t totalEvents = varEventPool.size();
        size_t numThreads = config["general"]["maxCores"].get<size_t>();
        if (numThreads == 0) numThreads = std::max(1u, std::thread::hardware_concurrency());
        size_t numChunks = numThreads * 4;
        if (numChunks > totalEvents) numChunks = std::max<size_t>(1, totalEvents);
        size_t chunkSize = (totalEvents + numChunks - 1) / numChunks;

        vector<vector<CoincRange>> chunkResults(numChunks);
        tbb::parallel_for(size_t(0), numChunks, [&](size_t chunkIdx) {
            size_t startIdx = chunkIdx * chunkSize;
            size_t endIdx = std::min(startIdx + chunkSize, totalEvents);
            if (startIdx >= endIdx) return;
            vector<int> checkMask(coincMask[0].size(), 0);
            auto &localMatches = chunkResults[chunkIdx];
            localMatches.reserve((endIdx - startIdx) / 16);
            for (size_t i = startIdx; i < endIdx; i++)
            {
                size_t crtPos = i;
                uint64_t startTime = varEventPool[i].getTimeStamp();
                uint64_t interval = varEventPool[crtPos].getTimeStamp() - startTime;
                std::fill(checkMask.begin(), checkMask.end(), 0);
                while (interval < coincWindow)
                {
                    addToCheckMask(checkMask, varEventPool[crtPos]);
                    crtPos++;
                    if (crtPos == totalEvents) break;
                    interval = varEventPool[crtPos].getTimeStamp() - startTime;
                }
                if (validateCheckMask(coincMask, checkMask))
                {
                    localMatches.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(crtPos)});
                }
            }
        });

        vector<CoincRange> eventPos;
        for (auto &chunk : chunkResults) eventPos.insert(eventPos.end(), chunk.begin(), chunk.end());
        vector<vector<CoincRange>>().swap(chunkResults);

        static vector<CoincRange> overlapEvents;
        overlapEvents.reserve(64);
        if (eventPos.size())
        {
            for (size_t i = 0; i < eventPos.size() - 1; i++)
            {
                int cnt = 1;
                auto d1 = eventPos[i].end - eventPos[i].start;
                auto d2 = eventPos[i + cnt].start - eventPos[i].start;
                overlapEvents.clear();
                overlapEvents.push_back(eventPos[i]);
                while (d2 < d1)
                {
                    overlapEvents.push_back(eventPos[i + cnt]);
                    cnt++;
                    if (i + cnt >= eventPos.size()) break;
                    d2 = eventPos[i + cnt].start - eventPos[i].start;
                }
                i += overlapEvents.size() - 1;
                auto evt = selectOverlapEvent(overlapEvents, varEventPool);
                postProcessEvent(config, evt, varEventPool);
            }
        }
        writer->closeFile();
        return;
    }

    // High-performance double-buffered sliding-window streaming engine (< 60 MB RAM)
    rpcClient->updateState({config["crtId"], "building events (3/4)", 0}, true);

    vector<event> eventRing;
    eventRing.reserve(1500000);

    const size_t BATCH_SIZE = 500000;
    const size_t MARGIN_EVENTS = 200000;

    vector<event> nextBatch;
    nextBatch.reserve(BATCH_SIZE + 64);

    vector<CoincRange> pendingMatches;
    vector<CoincRange> overlapGroup;

    size_t totalRead = 0;
    size_t loopIter = 0;

    // Launch background prefetch of initial batch
    auto readFuture = std::async(std::launch::async, [&]() {
        return loader->readEventsBatch(nextBatch, BATCH_SIZE);
    });

    while (true)
    {
        size_t nRead = readFuture.get();
        totalRead += nRead;
        bool isLast = loader->isStreamEof();

        if (!nextBatch.empty())
        {
            eventRing.insert(eventRing.end(), nextBatch.begin(), nextBatch.end());
            nextBatch.clear();
        }

        // Launch background prefetch for the NEXT batch immediately!
        if (!isLast)
        {
            readFuture = std::async(std::launch::async, [&]() {
                return loader->readEventsBatch(nextBatch, BATCH_SIZE);
            });
        }

        if (eventRing.empty()) break;

        // Sort current buffer in-place using tbb::parallel_sort
        tbb::parallel_sort(eventRing.begin(), eventRing.end(), [](const event &i, const event &j) {
            return i.getTimeStamp() < j.getTimeStamp();
        });

        // Determine safe limit to evaluate coincidences
        size_t evalLimit = eventRing.size();
        if (!isLast)
        {
            if (evalLimit > MARGIN_EVENTS)
            {
                evalLimit -= MARGIN_EVENTS;
            }
            else
            {
                continue;
            }
            uint64_t safeTime = eventRing[evalLimit].getTimeStamp();
            while (evalLimit > 0 && (safeTime - eventRing[evalLimit - 1].getTimeStamp() < coincWindow))
            {
                evalLimit--;
            }
        }

        // Parallel Coincidence Search using tbb::parallel_for
        size_t numChunks = std::min<size_t>(10, std::max<size_t>(1, evalLimit / 10000));
        if (numChunks <= 1)
        {
            vector<int> checkMask(coincMask[0].size(), 0);
            pendingMatches.clear();
            for (size_t i = 0; i < evalLimit; i++)
            {
                size_t crtPos = i;
                uint64_t startTime = eventRing[i].getTimeStamp();
                uint64_t interval = eventRing[crtPos].getTimeStamp() - startTime;
                std::fill(checkMask.begin(), checkMask.end(), 0);
                while (interval < coincWindow)
                {
                    addToCheckMask(checkMask, eventRing[crtPos]);
                    crtPos++;
                    if (crtPos == eventRing.size()) break;
                    interval = eventRing[crtPos].getTimeStamp() - startTime;
                }
                if (validateCheckMask(coincMask, checkMask))
                {
                    pendingMatches.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(crtPos)});
                }
            }
        }
        else
        {
            size_t chunkSize = (evalLimit + numChunks - 1) / numChunks;
            vector<vector<CoincRange>> chunkResults(numChunks);
            tbb::parallel_for(size_t(0), numChunks, [&](size_t chunkIdx) {
                size_t startIdx = chunkIdx * chunkSize;
                size_t endIdx = std::min(startIdx + chunkSize, evalLimit);
                if (startIdx >= endIdx) return;
                vector<int> checkMask(coincMask[0].size(), 0);
                auto &localMatches = chunkResults[chunkIdx];
                localMatches.reserve((endIdx - startIdx) / 16);
                for (size_t i = startIdx; i < endIdx; i++)
                {
                    size_t crtPos = i;
                    uint64_t startTime = eventRing[i].getTimeStamp();
                    uint64_t interval = eventRing[crtPos].getTimeStamp() - startTime;
                    std::fill(checkMask.begin(), checkMask.end(), 0);
                    while (interval < coincWindow)
                    {
                        addToCheckMask(checkMask, eventRing[crtPos]);
                        crtPos++;
                        if (crtPos == eventRing.size()) break;
                        interval = eventRing[crtPos].getTimeStamp() - startTime;
                    }
                    if (validateCheckMask(coincMask, checkMask))
                    {
                        localMatches.push_back({static_cast<uint32_t>(i), static_cast<uint32_t>(crtPos)});
                    }
                }
            });
            pendingMatches.clear();
            for (auto &chunk : chunkResults)
            {
                pendingMatches.insert(pendingMatches.end(), chunk.begin(), chunk.end());
            }
        }

        // Overlap resolution and writing
        size_t mIdx = 0;
        size_t lastEvictedEvtIdx = 0;

        while (mIdx < pendingMatches.size())
        {
            if (mIdx == pendingMatches.size() - 1 && !isLast)
            {
                break;
            }
            overlapGroup.clear();
            overlapGroup.push_back(pendingMatches[mIdx]);
            size_t cnt = 1;
            bool clusterClosed = false;

            while (mIdx + cnt < pendingMatches.size())
            {
                auto d1 = pendingMatches[mIdx].end - pendingMatches[mIdx].start;
                auto d2 = pendingMatches[mIdx + cnt].start - pendingMatches[mIdx].start;
                if (d2 < d1)
                {
                    overlapGroup.push_back(pendingMatches[mIdx + cnt]);
                    cnt++;
                }
                else
                {
                    clusterClosed = true;
                    break;
                }
            }

            if (!clusterClosed && !isLast)
            {
                break;
            }

            if (isLast && mIdx >= pendingMatches.size() - 1)
            {
                break;
            }

            auto evt = selectOverlapEvent(overlapGroup, eventRing);
            postProcessEvent(config, evt, eventRing);

            lastEvictedEvtIdx = pendingMatches[mIdx].end;
            mIdx += overlapGroup.size();
        }

        if (lastEvictedEvtIdx > 0 && !isLast)
        {
            eventRing.erase(eventRing.begin(), eventRing.begin() + lastEvictedEvtIdx);
        }

        if ((loopIter++ & 0x7) == 0)
        {
            rpcClient->updateState({config["crtId"], "building events (3/4)", loader->getStreamProgress()});
        }

        if (isLast) break;
    }

    loader->closeStream();
    rpcClient->updateState({config["crtId"], "filter/write events (4/4)", 100}, true);
    writer->closeFile();
}

CoincRange selectOverlapEvent(const vector<CoincRange> &events, const vector<event> &eventPool)
{

    if (events.size() == 1)
    {
        return events[0];
    }

    int maxValue = 0;

    for (auto &evt : events)
    {
        int sz = evt.end - evt.start;
        if (sz > maxValue)
        {
            maxValue = sz;
        }
    }

    int bestIdx = -1;
    double minStd = std::numeric_limits<double>::max();

    for (size_t i = 0; i < events.size(); i++)
    {
        if ((int)(events[i].end - events[i].start) == maxValue)
        {
            double stdev = getTimeStd(events[i], eventPool);
            if (stdev < minStd)
            {
                minStd = stdev;
                bestIdx = i;
            }
        }
    }

    return events[bestIdx];
}

double getTimeStd(const CoincRange &evt, const vector<event> &eventPool)
{
    uint32_t size = evt.end - evt.start;
    if (size <= 1)
    {
        return 0.0;
    }

    uint64_t t0 = eventPool[evt.start].getTimeStamp();
    double sum = 0.0;
    for (uint32_t i = evt.start; i < evt.end; i++)
    {
        sum += (double)(eventPool[i].getTimeStamp() - t0);
    }

    double mean = sum / size;

    double sq_sum = 0.0;
    for (uint32_t i = evt.start; i < evt.end; i++)
    {
        double diff = (double)(eventPool[i].getTimeStamp() - t0) - mean;
        sq_sum += diff * diff;
    }

    return sqrt(sq_sum / size);
}

void addToCheckMask(vector<int> &checkMask, const event &evt)
{
    if (evt.getDetectorType() < checkMask.size())
    {
        checkMask[evt.getDetectorType()]++;
    }
}

bool validateSingleMask(const vector<int> &coincMask, const vector<int> &checkMask)
{

    for (size_t i = 0; i < coincMask.size(); i++)
    {
        if (checkMask[i] < coincMask[i])
        {
            return false;
        }
    }
    return true;
}

bool validateCheckMask(const vector<vector<int>> &coincMask, const vector<int> &checkMask)
{

    for (size_t i = 0; i < coincMask.size(); i++)
    {
        if (validateSingleMask(coincMask[i], checkMask))
        {
            return true;
        }
    }

    return false;
}

string extractNumbers(const string &a, string format)
{

    string numberString;

    vector<string> sFormat = {"proto", "root", "cap", "bell", "compass", "compassWave", "test", "timepix"};

    if (find(sFormat.begin(), sFormat.end(), format) != sFormat.end())
    {
        auto pos = a.find_last_of(".");
        auto nSize = 6;

        numberString = a.substr(pos - nSize, nSize);
    }
    else if (format == "delila")
    {
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

void postProcessEvent(json &config, const CoincRange &evt, const vector<event> &eventPool)
{

    static vector<event> tempList;
    static vector<event *> ptrList;
    tempList.clear();
    ptrList.clear();

    tempList.reserve(evt.end - evt.start);

    uint64_t absTime = eventPool[evt.start].getTimeStamp();

    // capture the list
    for (uint32_t i = evt.start; i < evt.end; i++)
    {
        tempList.push_back(eventPool[i]);
    }

    ptrList.reserve(tempList.size());
    for (size_t i = 0; i < tempList.size(); i++)
    {
        ptrList.push_back(&tempList[i]);
    }

    if (!validateEvent(&ptrList))
    {
        return;
    }

    // sort by (type and timestamp)
    auto start = ptrList.begin();
    for (auto &val : typePriority)
    {
        auto it = std::partition(start, ptrList.end(), [val](event *ev)
                                 { return ev->getDetectorType() == val; });

        sort(start, it, [](event *i, event *m)
             { return i->getTimeStamp() < m->getTimeStamp(); });

        start = it;
    }

    // set relative times
    for (size_t i = 0; i < ptrList.size(); i++)
    {
        int32_t relativeTime = int64_t(ptrList[i]->getTimeStamp()) - int64_t(ptrList.front()->getTimeStamp()) + coincWindow;

        ptrList[i]->setRelativeTime(relativeTime);
    }

    writer->addBuiltEvent(ptrList, absTime);
}

bool validateEvent(const vector<event *> *events)
{
    size_t n = events->size();
    if (n <= 1)
    {
        return true;
    }

    // remove events with duplicate signals
    for (size_t i = 0; i < n; i++)
    {
        auto lut_i = (*events)[i]->getDetectorLut();
        auto type_i = (*events)[i]->getDetectorType();

        for (size_t j = i + 1; j < n; j++)
        {
            if (lut_i == (*events)[j]->getDetectorLut() &&
                type_i == (*events)[j]->getDetectorType())
            {
                return false;
            }
        }
    }

    return true;
}

template <class... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template <class... Ts>
overload(Ts...) -> overload<Ts...>;

void delayRun(json config)
{

    string dataType = config["input"]["fileFormat"];

    vector<tuple<int, int, uint64_t>> events = loader->readDelayEvents();

    sort(events.begin(), events.end(),
         [](auto i, auto j)
         { return (get<2>(i) < get<2>(j)); });

    double refTimeStamp = 0;
    bool refTimeFound = false;

    map<pair<int, int>, shared_ptr<TH1I>> hists;

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];

    for (auto &evt : events)
    {

        if (get<0>(evt) == refType && get<1>(evt) == refLUT)
        {
            refTimeStamp = get<2>(evt);
            refTimeFound = true;
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

            transform(range.begin(), range.end(), range.begin(), [&](auto rg)
                      { return rg * int(config["general"]["timeUnitMultiplier"]); });

            hists[id] = make_shared<TH1I>(TH1I(name.c_str(), name.c_str(), bins, range[0], range[1]));
        }
    }

    if (refTimeFound == false)
    {
        cout << "delay run failed: reference time not found" << endl;
        exit(1);
    }

    vector<vector<int>> delay = config["detector"]["delay"];

    for (auto &board : delay)
    {
        for (auto &ch : board)
        {
            ch = 0;
        }
    }

    for (auto hist : hists)
    {
        if (hist.second->GetEntries())
        {
            int binmax = hist.second->GetMaximumBin();
            double x = hist.second->GetXaxis()->GetBinCenter(binmax);

            auto brdCh = findBoardChannel(config, hist.first.first, hist.first.second);

            if (brdCh.has_value())
            {

                auto time = x;

                if (hist.first.first == refType && hist.first.second == refLUT)
                {
                    time = 0;
                }
                else
                {
                    time += int(config["delay"]["refTimeOffset"]) * int(config["general"]["timeUnitMultiplier"]);
                }

                delay[brdCh.value().first][brdCh.value().second] = time / int(config["general"]["timeUnitMultiplier"]);
            }
        }
    }

    config["detector"]["delay"] = delay;

    config.erase("crtFile");
    config.erase("crtRun");

    auto oFile = ofstream("config.json");
    SaxPrinter pretty(oFile);

    json::sax_parse(config.dump(), &pretty);

    writer->writeDelayResults(hists);
}

void convertRun(json config)
{
    vector<event> varEventPool = loader->readEventsFlat();

    for (auto &evt : varEventPool)
    {
        writer->addConvertEvent(&evt);
    }

    writer->closeFile();
}

void alignRun(json config)
{

    auto inpData = loader->readAlignEvents();

    auto runNumb = stoi(extractNumbers(config["crtFile"], config["input"]["fileFormat"]));

    loader->setCrtFile(config["refFile"]);

    auto refData = loader->readAlignEvents();

    vector<shared_future<calibPoint>> futures;

    for (auto &dp : refData)
    {
        auto id = dp.first;

        auto rData = refData[id];
        auto iData = inpData[id];

        futures.push_back(async(std::launch::async, [config, rData, iData, id, runNumb]() -> calibPoint
                                {
            auto calibrator = geneticOptimizer(config["align"]);
            calibrator.optimize(rData, iData);

            calibPoint point;

            point.runNumber = runNumb;
            point.type = id.first;
            point.lut = id.second;

            point.calibParam = calibrator.getBestPos();

            return point; })
                              .share());
    }

    vector<calibPoint> calibPoints;

    for (auto &f : futures)
    {
        auto calib = f.get();

        calibPoints.push_back(calib);
    }

    writer->writeAlignResults(calibPoints);
}