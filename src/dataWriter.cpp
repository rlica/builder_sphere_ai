#include "dataWriter.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <iomanip>
#include <set>
#include <unistd.h>

dataWriter::dataWriter(json &config) : varConfig(config)
{
    auto runType = config["general"]["run"].get<string>();

    if (runType == "build" || runType == "convert")
    {
        varFileName = config["run"]["fileName"].get<string>();
        varRunNumb = config["run"]["runNumber"].get<int>();
    }
}

gaspDataWriter::gaspDataWriter(json &config) : dataWriter(config)
{
    setConfig(config);

    validateConfig();

    auto runType = config["general"]["run"].get<string>();

    if (runType == "build")
    {
        initializeWriterStructures();

        initializeOutputFile();
        initializeRecordHeader();
    }
}

[[deprecated("not finished")]] void gaspDataWriter::validateConfig()
{
}

void gaspDataWriter::writeData()
{
    varOutFile.write((char *)&varBlockBuffer[0], varBlockBuffer.size() * sizeof(short));

    incrementBlockNumber();
}

void gaspDataWriter::initializeFileHeader()
{
    varBlockBuffer.push_back(0x0010);
    varBlockBuffer.push_back(0x0001);
    varBlockBuffer.push_back(getRunNumber());
    varBlockBuffer.push_back(0x4748);
    varBlockBuffer.push_back(0x0010);

    varBlockBuffer.insert(varBlockBuffer.end(), MAX_BLOCK_SIZE - varBlockBuffer.size(), 0);

    writeData();

    initializeRecordHeader();
}

void gaspDataWriter::initializeRecordHeader()
{
    varBlockBuffer.clear();

    varBlockBuffer.push_back(0x0010);
    varBlockBuffer.push_back(0x0000);
    varBlockBuffer.push_back(0x0000); // run number
    varBlockBuffer.push_back(0x4758);
    varBlockBuffer.push_back(EVENT_HEADER_SIZE);

    varBlockBuffer.insert(varBlockBuffer.end(), EVENT_HEADER_SIZE - varBlockBuffer.size(), 0);
}

void gaspDataWriter::closeFile()
{
    varBlockBuffer.insert(varBlockBuffer.end(), MAX_BLOCK_SIZE - varBlockBuffer.size(), 0);
    writeData();

    varOutFile.close();
    varOutFile.clear();
}

void gaspDataWriter::initializeOutputFile()
{

    resetBlockNumber();

    varOutFile.open(getFileName().c_str());

    initializeFileHeader();
}

[[deprecated]] void gaspDataWriter::setConfig(json &config)
{
    varConfig = config;

    resetGap = int(varConfig["build"]["absoluteTime"]["resetGap"]) * pow(10, 12);
    timeScale = pow(10, int(varConfig["build"]["absoluteTime"]["scalePower"]));
}

void rootDataWriter::setConfig(json &config)
{
    if (config["general"]["run"].get<string>() == "convert")
    {
        varAddHistogram = config["convert"]["root"]["addHistogram"].get<bool>();
        varAddList = config["convert"]["root"]["addList"].get<bool>();
        varAddRunTime = config["convert"]["root"]["addRunTime"].get<bool>();
    }

    resetGap = int(varConfig["build"]["absoluteTime"]["resetGap"]) * pow(10, 12);
    timeScale = pow(10, int(varConfig["build"]["absoluteTime"]["scalePower"]));
}

rootDataWriter::rootDataWriter(json &config) : dataWriter(config)
{
    auto runType = config["general"]["run"].get<string>();

    setConfig(config);

    if (runType == "build")
    {
        initializeBuildOutputFile();
    }
    else if (runType == "convert")
    {
        initializeConvertOutputFile();
    }
}

void rootDataWriter::initializeBuildOutputFile()
{

    string fName = getFileName() + ".root";

    varOutFile = make_unique<TFile>(fName.c_str(), "RECREATE");

    initializeBuildTree();
}

void rootDataWriter::initializeConvertOutputFile()
{
    string fName = getFileName() + ".root";

    varOutFile = make_unique<TFile>(fName.c_str(), "RECREATE");

    if (varAddList)
    {
        initializeConvertTree();
    }
}

[[deprecated]] void rootDataWriter::initializeBuildTree()
{
    varEventTree = new TTree("events", "events tree");

    varEventTree->Branch("detectorLUT", &varDetectorLUT);
    varEventTree->Branch("detectorType", &varDetectorType);

    varEventTree->Branch("energy", &varEnergy);
    varEventTree->Branch("psd", &varPsd);

    varEventTree->Branch("relativeNsTime", &varRelativeNsTime);
    varEventTree->Branch("relativePsTime", &varRelativePsTime);

    varEventTree->Branch("absoluteTime", &varAbsoluteTime);
}

void rootDataWriter::initializeConvertTree()
{
    varEventTree = new TTree("signals", "signals tree");

    varEventTree->Branch("board", &varConvBoard);
    varEventTree->Branch("channel", &varConvChannel);

    varEventTree->Branch("energy", &varConvEnergy);
    varEventTree->Branch("timestamp", &varConvTimestamp);
}

void rootDataWriter::addBuiltEvent(vector<event *> &events, uint64_t time)
{

    varDetectorLUT.clear();
    varDetectorType.clear();

    varEnergy.clear();

    varPsd.clear();

    varRelativeNsTime.clear();
    varRelativePsTime.clear();

    // add header parameters
    if (time > (prevTime + resetGap))
    {
        resetTime = time;
    }

    prevTime = time;

    varAbsoluteTime = (time - resetTime) * timeScale;

    for (auto sig : events)
    {

        varDetectorLUT.push_back(sig->getDetectorLut());
        varDetectorType.push_back(sig->getDetectorType());

        varEnergy.push_back(sig->getEnergy());
        varPsd.push_back(sig->getPSD());

        uint16_t fTime = sig->getRelativeTime() / 1000;
        uint16_t sTime = sig->getRelativeTime() % 1000;

        varRelativeNsTime.push_back(fTime);
        varRelativePsTime.push_back(sTime);
    }

    varEventTree->Fill();
}

void rootDataWriter::closeFile()
{

    auto runType = varConfig["general"]["run"].get<string>();

    if (runType == "convert")
    {
        if (varAddList)
        {
            varEventTree->Write();
        }

        if (varAddHistogram)
        {
            for (auto &hist : varEnergyHist)
            {
                hist.second->Write();
            }
        }

        if (varAddRunTime)
        {
            double time = double(stopTime - startTime) * pow(10, -12);
            TParameter<double> varRunTime("runTime", time);

            varRunTime.Write();
        }
    }
    else if (runType == "build")
    {
        varOutFile->cd();

        varEventTree->Write();
    }

    varOutFile->Close();
}

void rootDataWriter::writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist)
{
    string outFileName = string(varConfig["output"]["path"]) + "/";
    outFileName += string(varConfig["delay"]["outFileName"]) + ".root";

    auto outFile = TFile(outFileName.c_str(), "RECREATE");

    for (auto &hist : delayHist)
    {
        hist.second->Write();
    }

    outFile.Close();
}

void gaspDataWriter::writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist)
{
    map<int, vector<short>> varBlockBuffers;
    vector<short> source = vector<short>(pow(2, 17), 0);

    for (auto &hist : delayHist)
    {
        auto handle = hist.first.first;
        auto lut = hist.first.second;

        auto target = hist.first.second * pow(2, 17);

        while (varBlockBuffers[handle].size() != target)
        {
            std::move(source.begin(), source.end(), std::back_inserter(varBlockBuffers[handle]));
        }

        for (int i = 0; i < pow(2, 16); i++)
        {
            if (i < hist.second->GetNbinsX())
            {
                int binContent = hist.second->GetBinContent(i + 1);

                varBlockBuffers[handle].push_back(binContent & 0xFFFF);
                varBlockBuffers[handle].push_back((binContent >> 16) & 0xFFFF);
            }
            else
            {
                varBlockBuffers[handle].push_back(0);
                varBlockBuffers[handle].push_back(0);
            }
        }
    }

    string outFileBase = string(varConfig["output"]["path"]) + "/";
    outFileBase += string(varConfig["delay"]["outFileName"]);

    for (auto &buff : varBlockBuffers)
    {
        string outFileName = outFileBase + "_" + to_string(buff.first);

        ofstream outFile(outFileName.c_str());

        outFile.write((char *)&buff.second[0], buff.second.size() * sizeof(short));

        outFile.close();
    }
}

void capDataWriter::initializeOutputFile()
{
    string fName = getFileName() + ".cap";

    setFileDescriptor(open(fName.c_str(), O_WRONLY | O_CREAT, 0644));
}

void capDataWriter::closeFile()
{
    close(getFileDescriptor());
}

void capDataWriter::writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist)
{
    throw "write delay results is not currently implemented";
}

capDataWriter::capDataWriter(json &config) : dataWriter(config)
{
    initializeOutputFile();
}

void rootDataWriter::addConvertEvent(event *event)
{
    if (varAddList)
    {
        varConvBoard = event->getBoardNumb();
        varConvChannel = event->getChannelNumb();

        varConvEnergy = event->getEnergy();
        varConvTimestamp = event->getTimeStamp();

        varEventTree->Fill();
    }

    if (varAddHistogram)
    {
        auto target = make_pair(event->getBoardNumb(), event->getChannelNumb());

        auto mapIt = varEnergyHist.find(target);

        if (mapIt == varEnergyHist.end())
        {
            varEnergyHist[target] = make_shared<TH1D>(("brd_" + to_string(event->getBoardNumb()) + "_ch_" + to_string(event->getChannelNumb())).c_str(), "energy", pow(2, 14), 0, pow(2, 14));
        }

        varEnergyHist[target]->Fill(event->getEnergy());
    }

    if (varAddRunTime)
    {
        if (event->getTimeStamp() < startTime)
        {
            startTime = event->getTimeStamp();
        }

        if (event->getTimeStamp() > stopTime)
        {
            stopTime = event->getTimeStamp();
        }
    }
}

void gaspDataWriter::writeAlignResults(vector<calibPoint> calibPoints)
{

    string fileName = varConfig["align"]["outFileName"];
    string outPath = varConfig["output"]["path"];

    for (auto &calib : calibPoints)
    {
        string outFile = outPath + "/" + fileName + "_" + to_string(calib.type) + ".cal";

        int fd = open(outFile.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);

        std::stringstream buffer;

        buffer << calib.runNumber << " " << static_cast<uint32_t>(calib.lut) << " " << calib.calibParam.size() << " ";

        for (auto &val : calib.calibParam)
        {
            buffer << val << " ";
        }
        buffer << endl;

        flock(fd, LOCK_EX);
        write(fd, buffer.str().c_str(), buffer.str().size());
        flock(fd, LOCK_UN); // Release lock
        close(fd);
    }
}

int gaspDataWriter::findLut(int iType, int iPair)
{
    auto pairs = varConfig["detector"]["coincPairs"].get<vector<vector<int>>>();
    auto types = varConfig["detector"]["type"].get<vector<vector<int>>>();
    auto luts = varConfig["detector"]["lut"].get<vector<vector<int>>>();

    for (size_t i = 0; i < pairs.size(); i++)
    {
        for (size_t o = 0; o < pairs[i].size(); o++)
        {
            auto dType = types[i][o];
            auto dLut = luts[i][o];
            auto dPair = pairs[i][o];

            if (dType == iType && dPair == iPair)
            {
                return dLut;
            }
        }
    }

    assert(false && "requested LUT not found in pair");

    return {};
}

void gaspDataWriter::initializeWriterStructures()
{

    initializeValueGetters();

    auto pairs = varConfig["detector"]["coincPairs"].get<vector<vector<int>>>();
    auto types = varConfig["detector"]["type"].get<vector<vector<int>>>();
    auto luts = varConfig["detector"]["lut"].get<vector<vector<int>>>();

    auto cfg = varConfig["gasp"];

    auto outputFormat = cfg["typeOutputFormat"].get<vector<string>>();

    for (size_t i = 0; i < pairs.size(); i++)
    {
        for (size_t o = 0; o < pairs[i].size(); o++)
        {
            auto dType = types[i][o];
            auto dLut = luts[i][o];
            auto dPair = pairs[i][o];

            // skip the disabled channels
            if (dType == -1)
            {
                continue;
            }

            auto format = outputFormat[dType];

            auto dList = cfg["detectors"][format]["type"].get<vector<int>>();

            for (int t = 0; t < dList.size(); t++)
            {
                int kLut = dLut;
                int kType = dList[t];

                if (dPair != 0)
                {
                    kLut = findLut(dList[t], dPair);
                }

                typeGroups[{kType, kLut}].push_back({dType, dLut, int(dList.size()), t});
            }
        }
    }

    for (auto &wKey : typeGroups)
    {
        for (auto &wVal : wKey.second)
        {
            if (!groupTypeLuts[{wVal[0], wVal[1]}].size())
            {
                groupTypeLuts[{wVal[0], wVal[1]}] = vector<pair<int, int>>(wVal[2]);
            }

            groupTypeLuts[{wVal[0], wVal[1]}][wVal[3]] = {wKey.first.first, wKey.first.second};
            groupGetters[{wVal[0], wVal[3]}] = typeGetters[wVal[0]][wVal[3]];
        }
    }

    // generate default events with zero values
    for (size_t i = 0; i < pairs.size(); i++)
    {
        for (size_t o = 0; o < pairs[i].size(); o++)
        {
            auto dType = types[i][o];
            auto dLut = luts[i][o];

            if (dType == -1)
            {
                continue;
            }

            auto evt = new event();

            evt->setDetectorType(dType);
            evt->setDetectorLut(dLut);

            evt->setEnergy(0);
            evt->setRelativeTime(0);
            evt->setPSD(0);

            defaultEvents[{dType, dLut}] = evt;
        }
    }

    //
    eventMaskSize = 0;
    for (int i = 0; i < outputFormat.size(); i++)
    {
        auto format = outputFormat[i];

        auto dList = cfg["detectors"][format]["type"].get<vector<int>>();

        if (dList.size() == 0)
        {
            continue;
        }

        outputTypeMapper[i] = eventMaskSize;

        eventMaskSize++;
    }
}

void gaspDataWriter::initializeValueGetters()
{

    valueGetters["energy"] = [](event *ev)
    {
        return ev->getEnergy();
    };

    valueGetters["time/1000"] = [](event *ev)
    {
        return ev->getRelativeTime() / 1000;
    };

    valueGetters["time%1000"] = [](event *ev)
    {
        return ev->getRelativeTime() % 1000;
    };

    valueGetters["psd"] = [](event *ev)
    {
        return ev->getPSD();
    };

    auto cfg = varConfig["gasp"];

    auto outputFormat = cfg["typeOutputFormat"].get<vector<string>>();

    for (size_t i = 0; i < outputFormat.size(); i++)
    {
        auto format = outputFormat[i];

        auto dataTypeList = cfg["detectors"][format]["dataType"].get<vector<string>>();
        auto typeList = cfg["detectors"][format]["type"].get<vector<int>>();

        if (!dataTypeList.size())
        {
            continue;
        }

        typeGetters[i] = vector<function<uint16_t(event *)>>(dataTypeList.size());

        for (size_t o = 0; o < dataTypeList.size(); o++)
        {
            typeGetters[i][o] = valueGetters[dataTypeList[o]];
        }
    }
}

void gaspDataWriter::addBuiltEvent(vector<event *> &events, uint64_t time)
{
    struct BuiltDetectorSlot {
        int expectedSize = 0;
        event *signals[8] = {nullptr};
    };
    static BuiltDetectorSlot slotTable[16][32];
    static vector<pair<int, int>> activeSlots;

    for (size_t i = 0; i < events.size(); i++)
    {
        auto evt = events[i];
        const auto &mapping = typeGroups[{evt->getDetectorType(), evt->getDetectorLut()}];

        for (const auto &iMap : mapping)
        {
            int oType = iMap[0];
            int oLut = iMap[1];
            int expectedSize = iMap[2];
            int slotIdx = iMap[3];

            if (oType >= 0 && oType < 16 && oLut >= 0 && oLut < 32)
            {
                auto &slot = slotTable[oType][oLut];
                if (slot.expectedSize == 0)
                {
                    slot.expectedSize = expectedSize;
                    std::fill(slot.signals, slot.signals + expectedSize, nullptr);
                    activeSlots.push_back({oType, oLut});
                }
                slot.signals[slotIdx] = evt;
            }
        }
    }

    // retrieve missing events
    for (const auto &id : activeSlots)
    {
        auto &slot = slotTable[id.first][id.second];
        auto gEvt = defaultEvents[id];

        for (int i = 0; i < slot.expectedSize; i++)
        {
            if (slot.signals[i] == nullptr)
            {
                slot.signals[i] = gEvt;
            }
        }
    }

    // Preserve exact std::map key ascending order
    std::sort(activeSlots.begin(), activeSlots.end());

    // dump to buffer and file without heap vector or map allocations
    static vector<int> eventMask;
    eventMask.assign(eventMaskSize, 0);

    static vector<short> eventBuffer;
    eventBuffer.clear();
    eventBuffer.reserve(128);
    int size = 0;

    eventBuffer.push_back(0); // size word

    if (time > (prevTime + resetGap))
    {
        resetTime = time;
    }
    prevTime = time;

    short scaledTime = (time - resetTime) * timeScale;
    eventBuffer.push_back(scaledTime);
    size += 1;

    for (size_t i = 0; i < eventMaskSize; i++)
    {
        eventBuffer.push_back(0);
    }
    size += eventMaskSize;

    for (const auto &id : activeSlots)
    {
        int iType = id.first;
        int iLut = id.second;
        int oType = outputTypeMapper[iType];
        auto &slot = slotTable[iType][iLut];

        eventBuffer.push_back(iLut);
        size += 1;

        for (int i = 0; i < slot.expectedSize; i++)
        {
            auto evt = slot.signals[i];
            const auto &getter = typeGetters[iType][i];
            auto value = getter(evt);

            eventBuffer.push_back(value);
            size += 1;
        }

        eventMask[oType] += 1;
        slot.expectedSize = 0;
    }

    activeSlots.clear();

    eventBuffer[0] = 0xF000 + size;

    int offset = 1 + headerSize;
    for (size_t i = 0; i < eventMaskSize; i++)
    {
        eventBuffer[i + offset] = eventMask[i];
    }

    if (varBlockBuffer.size() + eventBuffer.size() < MAX_BLOCK_SIZE)
    {
        varBlockBuffer.insert(varBlockBuffer.end(), eventBuffer.begin(), eventBuffer.end());
    }
    else
    {
        varBlockBuffer.insert(varBlockBuffer.end(), MAX_BLOCK_SIZE - varBlockBuffer.size(), 0);
        writeData();
        initializeRecordHeader();
        varBlockBuffer.insert(varBlockBuffer.end(), eventBuffer.begin(), eventBuffer.end());
    }
}


void gaspDataWriter::dumpToFile(const map<pair<int, int>, vector<event *>> &events, uint64_t time)
{
    static vector<int> eventMask;
    eventMask.assign(eventMaskSize, 0);

    static vector<short> eventBuffer;
    eventBuffer.clear();
    eventBuffer.reserve(128);
    int size = 0;

    // add header
    eventBuffer.push_back(0); // size word

    // add header parameters
    if (time > (prevTime + resetGap))
    {
        resetTime = time;
    }

    prevTime = time;

    short scaledTime = (time - resetTime) * timeScale;

    // fill header parameters
    eventBuffer.push_back(scaledTime);

    size += 1; // the size word is not counted
    //

    // add event mask locations
    for (size_t i = 0; i < eventMask.size(); i++)
    {
        eventBuffer.push_back(0);
    }

    // all positions for the event mask
    size += eventMask.size();

    for (auto &gEvt : events)
    {
        auto iType = gEvt.first.first;
        auto iLut = gEvt.first.second;
        auto oType = outputTypeMapper[iType];

        // set lut
        eventBuffer.push_back(iLut);
        size += 1;

        for (size_t i = 0; i < gEvt.second.size(); i++)
        {
            auto evt = gEvt.second[i];
            const auto &getter = typeGetters[iType][i];
            auto value = getter(evt);

            eventBuffer.push_back(value);
            size += 1;
        }

        eventMask[oType] += 1;
    }

    // set final size
    eventBuffer[0] = 0xF000 + size;

    // set event mask
    int offset = 1 + headerSize;
    for (size_t i = 0; i < eventMask.size(); i++)
    {
        eventBuffer[i + offset] = eventMask[i];
    }

    // dump to file
    if (varBlockBuffer.size() + eventBuffer.size() < MAX_BLOCK_SIZE)
    {
        varBlockBuffer.insert(varBlockBuffer.end(), eventBuffer.begin(), eventBuffer.end());
    }
    else
    {
        // fill the end of buffer with zeros
        varBlockBuffer.insert(varBlockBuffer.end(), MAX_BLOCK_SIZE - varBlockBuffer.size(), 0);

        // write the buffer to disk
        writeData();

        // clear the buffer
        initializeRecordHeader();

        varBlockBuffer.insert(varBlockBuffer.end(), eventBuffer.begin(), eventBuffer.end());
    }
}