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

#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <fcntl.h>
#include <limits>

#define TRIGGER_DELAY 1000
#define DETECTOR_TYPES 5

std::mutex g_i_mutex;

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

json readConfigFile(string fileName);

deque<event *> convertROOTChain(json &config);
deque<event *> convertProto(json &config);

void builder(json &config, deque<event *> varEventPool, vector<vector<int>> coincMask);
bool validateCheckMask(vector<vector<int>> &coincMask, vector<int> &checkMask);
bool validateSingleMask(vector<int> &coincMask, vector<int> &checkMask);
void addToCheckMask(vector<int> &checkMask, event &evt);
void preProcessEvent(json &config, deque<event *> &events);
pair<int, int> findBoardChannel(json &config, int detType, int lut);
vector<string> getPathFiles(string path, string extension = "");
bool dataSort(const std::string &a, const std::string &b);
string extractNumbers(const string &a);

void postProcessEvent(pair<deque<event *>::iterator, deque<event *>::iterator>, shared_ptr<gaspDataWriter> dataWriter);

pair<int, int> findCoincPairType(json &config, int brd, int ch);
vector<string> removeAlreadyBuilt(vector<string> input, vector<string> output);
vector<int> getStartCond(vector<vector<int>> coincMask);
pair<deque<event *>::iterator, deque<event *>::iterator> selectOverlapEvent(vector<pair<deque<event *>::iterator, deque<event *>::iterator>> events);
double getTimeStd(pair<deque<event *>::iterator, deque<event *>::iterator> evt);

void analizeData(json config);

vector<shared_ptr<TH1D>> hists;
vector<shared_ptr<TH1D>> energy;

int main(int argc, char **argv)
{

    if (argc > 1)
    {

        json request = json::parse(argv[1]);

        analizeData(request);
    }
}

vector<string> removeAlreadyBuilt(vector<string> input, vector<string> output)
{

    for (auto i : output)
    {
        auto pos = i.find_last_of("/") + 1;
        auto tempOut = i.substr(pos);

        for (string &o : input)
        {
            pos = o.find_last_of("/") + 1;
            auto tempInp = o.substr(pos);

            if (tempInp.find(tempOut) != string::npos)
            {
                o = "";
            }
        }
    }

    input.erase(std::remove(input.begin(), input.end(), ""), input.end());

    return input;
}

vector<string> getPathFiles(string path, string extension)
{

    vector<pair<string, int64_t>> timeFiles;

    for (const auto &entry : fs::directory_iterator(path))
    {
        auto time = std::chrono::duration_cast<std::chrono::seconds>(entry.last_write_time().time_since_epoch()).count();

        if (extension != "")
        {
            if (entry.path().string().find(extension) != string::npos)
            {
                timeFiles.push_back(make_pair(entry.path().string(), time));
            }
        }
        else
        {
            timeFiles.push_back(make_pair(entry.path().string(), time));
        }
    }

    sort(timeFiles.begin(), timeFiles.end(),
         [](auto i, auto j)
         { return (i.second < j.second); });

    vector<string> files;
    files.resize(timeFiles.size());

    transform(timeFiles.cbegin(), timeFiles.cend(), files.begin(), [](pair<string, int64_t> i)
              { return i.first; });

    return files;
}

void analizeData(json config)
{

    string dataType = config["inputType"];

    deque<event *> events;

    if (dataType == "root")
    {
        events = convertROOTChain(config);
    }
    else if (dataType == "proto")
    {
        events = convertProto(config);
    }

    vector<vector<int>>
        coincMask = config["coincMask"].get<vector<vector<int>>>();

    builder(config, events, coincMask);
}

pair<int, int> findBoardChannel(json &config, int detType, int lut)
{

    vector<vector<int>> lutVect = config["detectorLUT"];
    vector<vector<int>> bTypeVect = config["detectorMask"];

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

    return {};
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

    uint eventNumb = config["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    string fileName = config["crtFile"];

    auto fd = open(fileName.c_str(), O_RDONLY);

    vector<vector<int>> delay = getDelayVector(config["delayNs"]);
    vector<vector<int>> lut = config["detectorLUT"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detectorMask"].get<vector<vector<int>>>();
    vector<vector<int>> coincPair = config["coincPairs"].get<vector<vector<int>>>();
    map<pair<int, int>, pair<int, int>> coincPairTypes;
    vector<int> antiCoincFlags = config["antiCoincFlag"].get<vector<int>>();

    kj::FdInputStream fdStream(fd);
    kj::BufferedInputStreamWrapper bufferedStream(fdStream);

    while (bufferedStream.tryGetReadBuffer() != nullptr)
    {
        capnp::PackedMessageReader message(bufferedStream);

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

deque<event *> convertROOTChain(json &config)
{

    vector<vector<int>> boards = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> channels = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> coincPairs = config["coincPairs"].get<vector<vector<int>>>();
    vector<vector<int>> delay = getDelayVector(config["delayNs"]);

    vector<int> antiCoincFlags = config["antiCoincFlag"].get<vector<int>>();

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

    int eventNumb = config["eventCount"];

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

json readConfigFile(string fileName)
{
    std::ifstream ifs(fileName.c_str());
    json jf = json::parse(ifs);

    return jf;
}

void preProcessEvent(json &config, deque<event *> &events)
{

    if (bool(config["sortBeforeBuild"]))
    {
        __gnu_parallel::sort(events.begin(), events.end(),
                             [](event *i, event *j)
                             { return (i->getTimeStamp() < j->getTimeStamp()); });
    }
}

vector<int> getStartCond(vector<vector<int>> coincMask)
{

    vector<int> tempCond = vector<int>(coincMask[0].size(), 0);

    for (auto cond : coincMask)
    {
        for (int i = 0; i < cond.size(); i++)
        {
            if (cond[i])
            {
                tempCond[i] = 1;
            }
        }
    }

    return tempCond;
}

void builder(json &config, deque<event *> varEventPool, vector<vector<int>> coincMask)
{

    preProcessEvent(config, varEventPool);

    string fileName = config["crtFile"];

    auto runNumber = stoi(extractNumbers(fileName));

    auto pos = fileName.find_last_of("/") + 1;
    auto nSize = 6;

    fileName = fileName.substr(pos, fileName.size());

    pos = fileName.find_last_of(".");

    fileName = fileName.substr(0, pos);

    fileName = string(config["outputPath"]) + "/" + fileName;

    auto dataWriter = make_shared<gaspDataWriter>(gaspDataWriter(fileName, runNumber));

    uint64_t coincWindow = config["coincWindow"];

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

            postProcessEvent(evt, dataWriter);
        }
    }

    for (auto i : varEventPool)
    {
        delete i;
    }

    dataWriter->closeFile();

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

bool dataSort(const std::string &a, const std::string &b)
{

    auto fSec = extractNumbers(a);
    auto sSec = extractNumbers(b);

    for (int i = 0; i < fSec.size(); i++)
    {
        if (fSec[i] > sSec[i])
        {
            return false;
        }
        else if (fSec[i] < sSec[i])
        {
            return true;
        }
    }

    return true;
}

string extractNumbers(const string &a)
{
    auto pos = a.find_last_of(".");
    auto nSize = 6;

    string numberString = a.substr(pos - nSize, nSize);

    return numberString;
}

void postProcessEvent(pair<deque<event *>::iterator, deque<event *>::iterator> evt, shared_ptr<gaspDataWriter> dataWriter)
{

    vector<event *> tempList;

    uint64_t absTime = (*evt.first)->getTimeStamp();

    for (auto i = evt.first; i < evt.second; i++)
    {
        tempList.push_back(new event(*(*i)));

        double relativeTime = (*i)->getTimeStamp() - tempList.front()->getTimeStamp() + TRIGGER_DELAY;

        uint16_t timeNs = int(relativeTime) / 1000;
        uint16_t timePs = int(relativeTime) % 1000;

        tempList.back()->setRelativeTimeNs(timeNs);
        tempList.back()->setRelativeTimePs(timePs);
    }

    if (tempList.size() != 1)
    {
        sort(tempList.begin(), tempList.end(), [](event *i, event *j)
             { return (i->getCoincPair() < j->getCoincPair()); });
    }

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
                        tempList[i]->setRelativeTimeNs(0);
                        tempList[i]->setRelativeTimePs(0);
                    }
                }
            }
        }
    }

    tempList.erase(remove(tempList.begin(), tempList.end(), nullptr), tempList.end());

    sort(tempList.begin(), tempList.end(), [](event *i, event *j)
         { return (i->getDetectorType() < j->getDetectorType()); });

    vector<int> eventMask = vector<int>(DETECTOR_TYPES, 0);

    for (auto i : tempList)
    {
        eventMask[i->getDetectorType()] += 1;
    }

    dataWriter->addBuiltEvent(&tempList, eventMask, absTime);

    for (int i = 0; i < tempList.size(); i++)
    {
        delete tempList[i];
    }
}

pair<int, int> findCoincPairType(json &config, int brd, int ch)
{

    int foundBrd = 0;

    int pair = config["coincPairs"][brd][ch];

    for (auto i : config["coincPairs"])
    {
        auto pos = find(i.begin(), i.end(), pair);

        if (pos != i.end())
        {

            if (brd != foundBrd || ch != distance(i.begin(), pos))
            {

                auto p = make_pair(config["detectorMask"][foundBrd][distance(i.begin(), pos)],
                                   config["detectorLUT"][foundBrd][distance(i.begin(), pos)]);

                return p;
            }
        }

        foundBrd += 1;
    }

    return make_pair(0, -100);
}
