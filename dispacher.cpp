#include <iostream>
#include "json.hpp"
#include <fstream>
#include "TH1D.h"
#include "TFile.h"
#include <filesystem>
#include <future>
#include "TTree.h"
#include "TPad.h"
#include <regex>
#include "utils.h"
#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <fcntl.h>
#include <limits>
#include <parallel/algorithm>
#include <cstdlib>

using namespace std;
using json = nlohmann::json;
namespace fs = std::filesystem;

json readConfigFile(string fileName);
void buildRun(json &config);
void delayRun(json &config);
void convertRun(json &config);
void alignRun(json &config);

vector<string> removeKeepFiles(vector<string> input, json config);
vector<string> removeAlreadyBuilt(vector<string> input, vector<string> output);
vector<string> getPathFiles(string path, string identifier = "", string extension = "");
void dispatchThread(json config);
vector<string> generateInputFiles(json &config);
void printStatus(int finished, int total);

int main(int argc, char **argv)
{
    json config = readConfigFile("config.json");

    try
    {

        try
        {
            auto schemaPath = getEnvironment("BUILDER_BASE") + "/configSchema.json";

            json schema = readConfigFile(schemaPath);

            validateJsonSchema(config, schema);
        }
        catch (string test)
        {
            cout << test << endl;
            exit(1);
        }

        if (config["general"]["run"] == "build")
        {
            buildRun(config);
        }
        else if (config["general"]["run"] == "delay")
        {
            delayRun(config);
        }
        else if (config["general"]["run"] == "convert")
        {
            convertRun(config);
        }
        else if (config["general"]["run"] == "align")
        {
            alignRun(config);
        }
        else
        {
            throw std::runtime_error("unknown run option, available options: build, delay, and convert");
        }
    }

    catch (const std::exception &e)
    {
        cout << "error: " << e.what() << endl;
    }
}

json readConfigFile(string fileName)
{

    std::ifstream ifs(fileName.c_str());
    json jf = json::parse(ifs);

    return jf;
}

void buildRun(json &config)
{
    auto inpFiles = generateInputFiles(config);

    vector<shared_future<void>> futures;

    int maxCores = config["general"]["maxCores"];

    cout << "build run:" << endl;
    cout << "requested: " << inpFiles.size() << " file/s" << endl;
    cout << "building on: " << maxCores << " core/s" << endl;

    int finished = 0;

    config["crtRun"] = "build";

    for (int i = 0; i < inpFiles.size(); i++)
    {
        config["crtFile"] = inpFiles[i];

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

vector<string> getPathFiles(string path, string identifier, string extension)
{

    vector<pair<string, int64_t>> timeFiles;

    for (const auto &entry : fs::directory_iterator(path))
    {
        auto time = std::chrono::duration_cast<std::chrono::seconds>(entry.last_write_time().time_since_epoch()).count();

        if (identifier != "")
        {

            if (extension != "")
            {
                bool fIdent = entry.path().string().find(identifier) != string::npos;
                bool fExt = entry.path().string().find(extension) != string::npos;

                if (fIdent && fExt)
                {
                    timeFiles.push_back(make_pair(entry.path().string(), time));
                }
            }
            else // condition for gasp output
            {
                bool fIdent = entry.path().string().find(identifier) != string::npos;
                bool fDot = entry.path().string().find(".") == string::npos;

                if (fIdent && fDot)
                {
                    timeFiles.push_back(make_pair(entry.path().string(), time));
                }
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

    for (auto out : output)
    {

        auto pos = out.find_last_of("/") + 1;
        auto tempOut = out.substr(pos);

        pos = tempOut.find_last_of(".");

        if (tempOut.find_last_of(".") != string::npos)
        {
            tempOut = tempOut.substr(0, pos);
        }

        for (string &inp : input)
        {
            if (inp.find(tempOut) != string::npos)
            {
                inp = "";
            }
        }
    }

    input.erase(std::remove(input.begin(), input.end(), ""), input.end());

    return input;
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

    string extension = fileExtensions[string(config["input"]["fileFormat"])];

    auto inpFiles = getPathFiles(config["input"]["path"], config["input"]["fileIdentifier"], extension);

    cout << "files: " << inpFiles.size() << " (total) ";

    if (bool(config["input"]["liveBuild"]) && inpFiles.size())
    {
        inpFiles.pop_back();
        cout << "-> " << inpFiles.size() << " (live) ";
    }

    if (inpFiles.size())
    {

        inpFiles = removeKeepFiles(inpFiles, config);

        cout << "-> " << inpFiles.size() << " (keepIndex) ";

        string extension = fileExtensions[string(config["output"]["fileFormat"])];

        auto outFiles = getPathFiles(config["output"]["path"], config["input"]["fileIdentifier"], extension);

        auto finalFiles = removeAlreadyBuilt(inpFiles, outFiles);

        cout << "-> " << finalFiles.size() << " (already done) " << endl;

        if (finalFiles.size())
        {
            cout << "info: " << finalFiles.front() << " " << finalFiles.back() << endl;
        }

        return finalFiles;
    }

    cout << endl;

    return {};
}

void delayRun(json &config)
{

    string extension = fileExtensions[string(config["input"]["fileFormat"])];

    auto inpFiles = getPathFiles(config["input"]["path"], config["input"]["fileIdentifier"], extension);

    inpFiles = removeKeepFiles(inpFiles, config);

    if (inpFiles.size())
    {
        cout << "running delay from: " << inpFiles.front() << " to " << inpFiles.back() << endl;

        config["crtFile"] = inpFiles;
        config["crtRun"] = "delay";

        dispatchThread(config);
    }
    else
    {
        cout << "no files found" << endl;
    }
}

void convertRun(json &config)
{

    auto inpFiles = generateInputFiles(config);

    vector<shared_future<void>> futures;

    int maxCores = config["general"]["maxCores"];

    cout << "convert run:" << endl;
    cout << "requested: " << inpFiles.size() << " file/s" << endl;
    cout << "building on: " << maxCores << " core/s" << endl;

    int finished = 0;

    config["crtRun"] = "convert";

    for (int i = 0; i < inpFiles.size(); i++)
    {
        config["crtFile"] = inpFiles[i];

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

vector<string> removeKeepFiles(vector<string> inpFiles, json config)
{

    int start = int(config["input"]["keepIndex"][0]);
    int stop = int(config["input"]["keepIndex"][1]);

    if (start <= 0)
    {
        start = 1;
    }

    if (stop <= 0)
    {
        stop = std::numeric_limits<int>::max();
    }

    vector<string> output;

    for (auto &str : inpFiles)
    {
        fs::path p(str);

        string stem = fs::path(str).stem().string();

        int index = 0;

        // if (config["input"]["fileFormat"] == "delila")
        // {
        //     auto fPos = stem.find_first_of("_") + 1;
        //     auto lPos = stem.find_last_of("_");

        //     index = stoi(stem.substr(fPos, lPos - fPos));
        // }
        // else
        {
            index = stoi(stem.substr(stem.length() - 6));
        }

        if (index >= start && index <= stop)
        {
            output.push_back(str);
        }
    }

    return output;
}

void checkAlignConfig(json &config)
{

    if (config.find("eliteSigma") == config.end())
    {
        config["eliteSigma"] = 0.1;
    }

    if (config.find("eliteSteps") == config.end())
    {
        config["eliteSteps"] = 2;
    }

    if (config.find("histBins") == config.end())
    {
        config["histBins"] = pow(2, 14);
    }

    if (config.find("histRange") == config.end())
    {
        config["histRange"] = {0, pow(2, 14)};
    }

    if (config.find("objectiveFunc") == config.end())
    {
        config["objectiveFunc"] = "pearson";
    }

    if (config.find("paraLimits") == config.end())
    {
        config["paraLimits"] = {{-20, 20}, {0.8, 1.2}};
    }

    if (config.find("population") == config.end())
    {
        config["objectiveFunc"] = 20;
    }

    if (config.find("selection") == config.end())
    {
        config["selection"] = {1, 1, 0.1};
    }

    if (config.find("steps") == config.end())
    {
        config["steps"] = 20;
    }
}

void alignRun(json &config)
{

    checkAlignConfig(config);

    string extension = fileExtensions[string(config["input"]["fileFormat"])];

    auto inpFiles = getPathFiles(config["input"]["path"], config["input"]["fileIdentifier"], extension);
    string refFile = config["align"]["refFile"];

    if (std::find_if(inpFiles.begin(), inpFiles.end(), [refFile](string file)
                     { return fs::path{file}.filename() == refFile; }) == inpFiles.end())
    {
        throw std::runtime_error("align: reference file not found in input files");
    }

    refFile = fs::path(config["input"]["path"]).string() + "/" + refFile;

    vector<shared_future<void>> futures;

    int maxCores = config["general"]["maxCores"];

    cout << "align run:" << endl;
    cout << "requested: " << inpFiles.size() << " file/s" << endl;
    cout << "building on: " << maxCores << " core/s" << endl;

    int finished = 0;

    config["crtRun"] = "align";
    config["refFile"] = refFile;

    for (int i = 0; i < inpFiles.size(); i++)
    {
        config["crtFile"] = inpFiles[i];

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