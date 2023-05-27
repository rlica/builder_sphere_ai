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

#include "TApplication.h"

using json = nlohmann::json;
using namespace std;

typedef unsigned __int128 uint128_t;
typedef __float128 float128_t;

void addToCheckMask(vector<int> &checkMask, event &evt);
void preProcessEvent(json &config, deque<event *> &events);
void postProcessEvent(json &config, pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void buildRun(json config);
void delayRun(json config);
bool validateEvent(vector<event *> *events);

bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask);
bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask);

pair<int, int> findBoardChannel(json &config, int detType, int lut);

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
    else
    {
        throw invalid_argument("Invalid data type");
    }
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

        // calculate raw timestamps
        // uint16_t rawRelativeTsNs = int64_t(tempList[i]->getRawTs() - int64_t(tempList.front()->getRawTs()));
        // uint16_t rawRelativeFineTs = int16_t(tempList[i]->getRawFineTs() - int16_t(tempList.front()->getRawFineTs()));
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
