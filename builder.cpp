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
#include "TApplication.h"

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

namespace fs = std::filesystem;
void addToCheckMask(vector<int> &checkMask, event &evt);
void preProcessEvent(json &config, deque<event *> &events);
void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void buildRun(json config);
void delayRun(json config);
bool validateEvent(vector<event *> *events);

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask);
bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask);

pair<uint8_t, uint8_t> findBoardChannel(json &config, uint8_t type, uint8_t lut);

string extractNumbers(const string &a, string format = "proto");

pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events);

double getTimeStd(pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void initializeDataLoader(json &config);

dataWriter *writer;
shared_ptr<dataLoader> loader;

int main(int argc, char **argv)
{

    TApplication theApp("theApp", &argc, argv);

    if (argc > 1)
    {

        json config = json::parse(argv[1]);

        initializeDataLoader(config);

        if (config["crtRun"] == "build")
        {
            buildRun(config);
        }
        else if (config["crtRun"] == "delay")
        {
            delayRun(config);
        }
    }
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
    else
    {
        throw invalid_argument("Invalid data type");
    }
}

pair<uint8_t, uint8_t> findBoardChannel(json &config, uint8_t type, uint8_t lut)
{

    vector<vector<uint8_t>> lutVect = config["detector"]["lut"];
    vector<vector<uint8_t>> bTypeVect = config["detector"]["type"];

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

    cout << "Board and channel not found for detType: " << type << " and lut: " << lut << endl;

    abort();

    return {};
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

void buildRun(json config)
{

    deque<event *> varEventPool = loader->readEvents();

    preProcessEvent(config, varEventPool);

    string fileName = config["crtFile"];

    auto runNumber = stoi(extractNumbers(fileName, config["input"]["fileFormat"]));

    fileName = fs::path(fileName).stem().string();

    fileName = string(config["output"]["path"]) + "/" + fileName;

    if (config["output"]["fileFormat"] == "gasp")
    {
        writer = new gaspDataWriter(fileName, runNumber);
    }
    else if (config["output"]["fileFormat"] == "root")
    {
        writer = new rootDataWriter(fileName, runNumber);
    }

    writer->setConfig(config);

    uint32_t coincWindow = uint32_t(config["build"]["coincWindow"]) * units[config["general"]["configTimeUnit"]];

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

    for (uint i = 0; i < coincMask.size(); i++)
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

    for (uint i = 0; i < coincMask.size(); i++)
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

    vector<string> sFormat = {"proto", "root", "cap", "bell"};

    if (find(sFormat.begin(), sFormat.end(), format) != sFormat.end())
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

    if (!validateEvent(&tempList))
    {
        return;
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

    uint32_t coincWindow = uint32_t(config["build"]["coincWindow"]) * units[config["general"]["configTimeUnit"]];

    for (int i = 0; i < tempList.size(); i++)
    {
        int32_t relativeTime = int64_t(tempList[i]->getTimeStamp()) - int64_t(tempList.front()->getTimeStamp()) + coincWindow;

        tempList[i]->setRelativeTime(relativeTime);
    }

    // build events with anticoincidence
    map<uint8_t, deque<event *>> coincMap;

    for (uint i = 0; i < tempList.size(); i++)
    {
        coincMap[tempList[i]->getCoincPair()].push_back(tempList[i]);
    }

    for (auto &val : coincMap)
    {

        // the zero mappings do not have pairs
        if (val.first != 0)
        {
            // one of the signals is missing
            if (val.second.size() == 1)
            {
                auto evt = new event();

                auto [type, lut] = val.second[0]->getCoincPairType();

                evt->setDetectorType(type);
                evt->setDetectorLut(lut);

                evt->setEnergy(0);
                evt->setRelativeTime(0);
                evt->setPSD(0);

                if (val.second[0]->getAntiCoincFlag()) // first signal is the anti-coinc flag -> push front
                {
                    val.second.push_front(evt);
                }
                else // second signal is the anti-coinc flag -> push back
                {
                    val.second.push_back(evt);
                }
            }
            else if (val.second.size() == 2) // both signals are here but the order might be wrong
            {
                if (val.second[0]->getAntiCoincFlag()) // first signal is the anticoinc flag -> reverse
                {
                    std::reverse(val.second.begin(), val.second.end());
                }
            }
            else
            {
                cout << "more then two coincident signals found!" << endl;
                abort();
            }
        }
    }

    vector<vector<event *>> eventList;

    for (auto &val : coincMap)
    {
        if (val.first == 0)
        {
            for (auto &evt : val.second)
            {
                eventList.push_back(vector<event *>(1, evt));
            }
        }
        else
        {
            eventList.push_back(vector<event *>(val.second.begin(), val.second.end()));
        }
    }
    // build events with anticoincidence

    std::sort(eventList.begin(), eventList.end(), [](auto i, auto j)
              { return (i[0]->getDetectorType() < j[0]->getDetectorType()); });

    writer->addBuiltEvent(&eventList, absTime);

    for (int i = 0; i < eventList.size(); i++)
    {
        for (int j = 0; j < eventList[i].size(); j++)
        {
            delete eventList[i][j];
        }
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

    map<pair<int, int>, shared_ptr<TH1I>> hists;

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

            transform(range.begin(), range.end(), range.begin(), [&](auto rg)
                      { return rg * units[config["general"]["configTimeUnit"]]; });

            hists[id] = make_shared<TH1I>(TH1I(name.c_str(), name.c_str(), bins, range[0], range[1]));
        }
    }

    vector<vector<int>> delay = config["detector"]["delay"];

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
                time += int(config["delay"]["refTimeOffset"]) * units[config["general"]["configTimeUnit"]];
            }

            delay[brdCh.first][brdCh.second] = time / units[config["general"]["configTimeUnit"]];
        }
    }

    config["detector"]["delay"] = delay;

    config.erase("crtFile");
    config.erase("crtRun");

    auto oFile = ofstream("config.json");
    SaxPrinter pretty(oFile);

    json::sax_parse(config.dump(), &pretty);

    if (config["output"]["fileFormat"] == "gasp")
    {
        writer = new gaspDataWriter();
    }
    else if (config["output"]["fileFormat"] == "root")
    {
        writer = new rootDataWriter();
    }

    writer->setConfig(config);

    writer->writeDelayResults(hists);
}
