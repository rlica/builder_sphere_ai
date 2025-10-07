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

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

void addToCheckMask(vector<int> &checkMask, event &evt);
void preProcessEvent(json &config, deque<event *> &events);
void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void buildRun(json config);
void delayRun(json config);
void convertRun(json config);
void alignRun(json config);

bool validateEvent(vector<event *> *events);

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask);
bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask);

optional<pair<uint8_t, uint8_t>> findBoardChannel(json &config, uint8_t type, uint8_t lut);

string extractNumbers(const string &a, string format = "proto");

pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events);

double getTimeStd(pair<deque<event *>::iterator, deque<event *>::iterator> evt);

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

void preProcessEvent(json &config, deque<event *> &events)
{

    if (bool(config["build"]["sortBeforeBuild"]))
    {
        std::sort(std::execution::par, events.begin(), events.end(),
                  [](event *i, event *j)
                  { return (i->getTimeStamp() < j->getTimeStamp()); });
    }
}

void buildRun(json config)
{
    deque<event *> varEventPool = loader->readEvents();

    preProcessEvent(config, varEventPool);

    uint32_t coincWindow = uint32_t(config["build"]["coincWindow"]) * int(config["general"]["timeUnitMultiplier"]);

    vector<vector<int>> coincMask = config["build"]["coincMask"].get<vector<vector<int>>>();

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
        for (size_t i = 0; i < eventPos.size() - 1; i++)
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

    for (auto &i : varEventPool)
    {
        delete i;
    }

    writer->closeFile();
}

pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events)
{

    if (events.size() == 1)
    {
        return events[0];
    }

    vector<int> evtSize;
    int maxValue = 0;

    for (auto &evt : events)
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

    uint128_t sum = accumulate(evt.first, evt.second, uint128_t(0), [](auto sum, auto val)
                               { return sum + val->getTimeStamp(); });

    float128_t mean = sum / size;

    vector<double> diff(size);

    transform(evt.first, evt.second, diff.begin(), [mean](auto x)
              { return float128_t(x->getTimeStamp()) - mean; });

    double sq_sum = inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
    double stdev = sqrt(sq_sum / size);

    return stdev;
}

void addToCheckMask(vector<int> &checkMask, event &evt)
{
    if (evt.getDetectorType() < checkMask.size())
    {
        checkMask[evt.getDetectorType()]++;
    }
}

bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask)
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

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask)
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

void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt)
{

    vector<event *> tempList;

    uint64_t absTime = (*evt.first)->getTimeStamp();

    // capture the list
    for (auto i = evt.first; i < evt.second; i++)
    {
        tempList.push_back(new event(*(*i)));
    }

    if (!validateEvent(&tempList))
    {
        return;
    }

    // sort by (type and timestamp)
    auto start = tempList.begin();
    for (auto &val : typePriority)
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
        int32_t relativeTime = int64_t(tempList[i]->getTimeStamp()) - int64_t(tempList.front()->getTimeStamp()) + coincWindow;

        tempList[i]->setRelativeTime(relativeTime);
    }

    writer->addBuiltEvent(tempList, absTime);

    for (int i = 0; i < tempList.size(); i++)
    {
        delete tempList[i];
    }
}

bool validateEvent(vector<event *> *events)
{

    // remove events with duplicate signals
    map<pair<int, int>, int> counts;

    for (auto &ev : *events)
    {
        counts[make_pair(ev->getDetectorLut(), ev->getDetectorType())]++;
    }

    if (counts.size() != events->size())
    {
        return false;
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
    deque<event *> varEventPool = loader->readEvents();

    for (auto &evt : varEventPool)
    {
        writer->addConvertEvent(evt);
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