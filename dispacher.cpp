#include <iostream>
#include "json.hpp"
#include <fstream>
#include "TH1D.h"
#include "TFile.h"
#include "TApplication.h"
#include <filesystem>
#include <future>
#include "TTree.h"
#include "TPad.h"
#include <regex>

using namespace std;
using json = nlohmann::json;
namespace fs = std::filesystem;

json readConfigFile(string fileName);
void buildData(json &config);
vector<string> removeAlreadyBuilt(vector<string> input, vector<string> output);
pair<int, int> findBoardChannel(json &config, int detType, int lut);
void calculateDelay(json &config);
vector<string> getPathFiles(string path, string extension = "");
void dispatchThread(json config);
vector<string> generateInputFiles(json &config);
void printStatus(int finished, int total);

int main(int argc, char **argv)
{
    TApplication theApp("theApp", &argc, argv);

    json config = readConfigFile("config.json");

    if (config["run"] == "build")
    {
        buildData(config);
    }
    else if (config["run"] == "delay")
    {
        calculateDelay(config);
    }
    else
    {
        cout << "unknown run option, available options: run, delay" << endl;
    }
}

json readConfigFile(string fileName)
{

    std::ifstream ifs(fileName.c_str());
    json jf = json::parse(ifs);

    return jf;
}

void buildData(json &config)
{

    auto inpFiles = generateInputFiles(config);

    vector<shared_future<void>> futures;

    int maxCores = config["maxCores"];

    cout << "requested: " << inpFiles.size() << " file/s" << endl;
    cout << "building on: " << maxCores << " core/s" << endl;

    int finished = 0;

    for (int i = 0; i < inpFiles.size(); i++)
    {
        config["rootFile"] = inpFiles[i];

        futures.push_back(async(std::launch::async, [config]()
                                { dispatchThread(config); })
                              .share());

        while (futures.size() == maxCores)
        {
            int pos = -1;

            for (int o = 0; o < futures.size(); o++)
            {
                if (futures[o].wait_for(10ms) == std::future_status::ready)
                {
                    pos = o;
                }
            }

            if (pos != -1)
            {
                futures.erase(futures.begin() + pos);
                finished++;
            }
        }
        printStatus(finished, inpFiles.size());
    }

    while (futures.size())
    {

        int pos = -1;

        for (int o = 0; o < futures.size(); o++)
        {
            if (futures[o].wait_for(10ms) == std::future_status::ready)
            {
                pos = o;
            }
        }

        if (pos != -1)
        {
            futures.erase(futures.begin() + pos);
            finished++;
        }

        printStatus(finished, inpFiles.size());
    }

    if (finished)
    {
        cout << endl;
    }
}

void printStatus(int finished, int total)
{
    string doneStr = "000000" + to_string(finished);
    doneStr = doneStr.substr(doneStr.size() - 6);

    string totalStr = "000000" + to_string(total);
    totalStr = totalStr.substr(totalStr.size() - 6);

    cout << "\rstatus: " << doneStr << " / " << totalStr << std::flush;
}

void calculateDelay(json &config)
{

    vector<vector<shared_ptr<TH1D>>> hists;

    for (int i = 0; i < 5; i++)
    {
        auto temp = vector<shared_ptr<TH1D>>();
        hists.push_back(temp);
        for (int o = 0; o < 25; o++)
        {
            string fileName = "type_" + to_string(i) + "_lut_" + to_string(o);
            hists[i].push_back(make_shared<TH1D>(TH1D(fileName.c_str(), fileName.c_str(), 10000, -10, 20000000)));
        }
    }

    auto inpFiles = getPathFiles(config["inputPath"], ".root");

    auto tFile = TFile(inpFiles[0].c_str());

    if (!tFile.IsZombie())
    {

        auto chain = dynamic_cast<TTree *>(tFile.Get("events"));

        uint16_t lut;
        uint16_t dType;
        ULong64_t timeStamp;
        uint16_t energy;

        chain->SetBranchAddress("boardTimeStamp", &timeStamp);
        chain->SetBranchAddress("detectorLUT", &lut);
        chain->SetBranchAddress("detectorType", &dType);
        chain->SetBranchAddress("energy", &energy);

        int eventNumb = config["eventCount"];

        if (eventNumb == 0)
        {
            eventNumb = chain->GetEntries();
        }

        vector<vector<uint64_t>> theTimes = vector<vector<uint64_t>>(5, vector<uint64_t>(0));

        vector<tuple<int, int, uint64_t>> events;

        for (int i = 0; i < eventNumb; i++)
        {
            chain->GetEntry(i);

            if (energy > 200)
            {
                events.push_back(make_tuple(dType, lut, timeStamp));
            }
        }

        sort(events.begin(), events.end(),
             [](auto i, auto j)
             { return (get<2>(i) < get<2>(j)); });

        uint64_t refTimeStamp;

        for (auto evt : events)
        {
            if (get<0>(evt) == 2 && get<1>(evt) == 0)
            {
                refTimeStamp = get<2>(evt);
            }

            int64_t timeDif = get<2>(evt) - refTimeStamp;

            hists[get<0>(evt)][get<1>(evt)]->Fill(timeDif);
        }

        vector<vector<int>> theDelays = vector<vector<int>>(5, vector<int>(16, 0));

        vector<vector<int>> lutVect = config["detectorLUT"];
        vector<vector<int>> bTypeVect = config["detectorMask"];

        for (int i = 0; i < hists.size(); i++)
        {
            for (int o = 0; o < hists[i].size(); o++)
            {
                if (hists[i][o]->GetEntries())
                {

                    if (i != 4)
                    {
                        int binmax = hists[i][o]->GetMaximumBin();
                        double x = hists[i][o]->GetXaxis()->GetBinCenter(binmax);

                        auto brdCh = findBoardChannel(config, i, o);

                        theDelays[brdCh.first][brdCh.second] = x / 1000;

                        hists[i][o]->Draw("hist");
                        gPad->WaitPrimitive("ggg");
                    }
                }
            }
        }

        config["delayNs"] = theDelays;

        std::ofstream outFile("config.json");
        outFile << std::setw(2) << config << std::endl;
    }
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

void dispatchThread(json config)
{
    string dump = config.dump();

    dump = std::regex_replace(dump, std::regex("\""), "\\\"");

    string cmd = "./builder \"" + dump + "\"";

    system(cmd.c_str());
}

vector<string> generateInputFiles(json &config)
{

    auto inpFiles = getPathFiles(config["inputPath"], config["inputIdentifier"]);

    cout << "files: " << inpFiles.size() << " (total) ";

    if (bool(config["liveBuild"]) && inpFiles.size())
    {
        inpFiles.pop_back();
        cout << "-> " << inpFiles.size() << " (live) ";
    }

    if (inpFiles.size())
    {

        int start = int(config["keep"][0]);
        int stop = int(config["keep"][1]);

        if (start < 0 || start >= inpFiles.size())
        {
            start = 0;
        }

        if (stop < 0 || stop >= inpFiles.size())
        {
            stop = inpFiles.size();
        }

        if (start == stop)
        {
            start = 0;
            stop = inpFiles.size();
        }

        inpFiles = {inpFiles.begin() + start, inpFiles.begin() + stop};

        cout << "-> " << inpFiles.size() << " (keep) ";

        auto outFiles = getPathFiles(config["outputPath"]);

        auto finalFiles = removeAlreadyBuilt(inpFiles, outFiles);

        cout << "-> " << finalFiles.size() << " (built) " << endl;

        if (finalFiles.size())
        {
            cout << "info: " << finalFiles.front() << " " << finalFiles.back() << endl;
        }

        return finalFiles;
    }

    return {};
}