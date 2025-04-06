#include "dataWriter.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <iomanip>
#include <set>

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

    initializeWriterStructures();

    initializeOutputFile();
    initializeRecordHeader();
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

    // resetGap = int(varConfig["build"]["absoluteTime"]["resetGap"]) * pow(10, 12);
    // timeScale = pow(10, int(varConfig["build"]["absoluteTime"]["scalePower"]));

    // auto dTypes = config["detector"]["type"].get<vector<vector<int>>>();

    // map<int, int> typeCount;

    // for (auto brd : dTypes)
    // {
    //     for (auto ch : brd)
    //     {
    //         typeCount[ch]++;
    //     }
    // }
    // typeCount.erase(-1);

    // auto outTypes = varConfig["gasp"]["typeOutputFormat"].get<vector<string>>();

    // if (outTypes.size() < typeCount.size())
    // {
    //     throw "gasp typeOutputFormat are not set for all detector types";
    // }

    // for (auto [type, count] : typeCount)
    // {

    //     if (outTypes[type] == "detector")
    //     {
    //         varOutputTypes[type] = DETECTOR;
    //     }
    //     else if (outTypes[type] == "detectorRaw")
    //     {
    //         varOutputTypes[type] = DETECTOR_RAW;
    //     }
    //     else if (outTypes[type] == "suppressedDetector")
    //     {
    //         varOutputTypes[type] = SUPPRESED_DETECTOR;
    //     }
    //     else if (outTypes[type] == "suppressedDetectorRaw")
    //     {
    //         varOutputTypes[type] = SUPPRESED_DETECTOR_RAW;
    //     }
    //     else if (outTypes[type] == "psdDetector")
    //     {
    //         varOutputTypes[type] = PSD_DETECTOR;
    //     }
    //     else if (outTypes[type] == "suppresedPsdDetector")
    //     {
    //         varOutputTypes[type] = SUPPRESED_PSD_DETECTOR;
    //     }
    //     else
    //     {
    //         varOutputTypes[type] = NO_TYPE;
    //     }
    // }

    // vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    // for (auto m : antiCoincFlags)
    // {
    //     varOutputTypes.erase(m);
    // }
}

void rootDataWriter::setConfig(json &config)
{
    varAddHistogram = config["convert"]["root"]["addHistogram"].get<bool>();
    varAddList = config["convert"]["root"]["addList"].get<bool>();
    varAddRunTime = config["convert"]["root"]["addRunTime"].get<bool>();
}

rootDataWriter::rootDataWriter(json &config) : dataWriter(config)
{
    setConfig(config);

    auto runType = config["general"]["run"].get<string>();

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

void rootDataWriter::initializeBuildTree()
{

    varEventTree = new TTree("events", "events tree");

    varEventTree->Branch("detectorLUT", &varDetectorLUT);
    varEventTree->Branch("detectorType", &varDetectorType);

    varEventTree->Branch("energy", &varEnergy);
    varEventTree->Branch("psd", &varPsd);

    varEventTree->Branch("vetoEnergy", &varVetoEnergy);

    varEventTree->Branch("relativeNsTime", &varRelativeNsTime);
    varEventTree->Branch("relativePsTime", &varRelativePsTime);

    varEventTree->Branch("vetoRelativeNsTime", &varVetoRelativeNsTime);
    varEventTree->Branch("vetoRelativePsTime", &varVetoRelativePsTime);
}

void rootDataWriter::initializeConvertTree()
{
    varEventTree = new TTree("signals", "signals tree");

    varEventTree->Branch("board", &varConvBoard);
    varEventTree->Branch("channel", &varConvChannel);

    varEventTree->Branch("energy", &varConvEnergy);
    varEventTree->Branch("timestamp", &varConvTimestamp);
}

void rootDataWriter::addBuiltEvent(vector<vector<event *>> *events, int64_t time)
{

    varDetectorLUT.clear();
    varDetectorType.clear();

    varEnergy.clear();

    varPsd.clear();

    varVetoEnergy.clear();

    varRelativeNsTime.clear();
    varRelativePsTime.clear();

    varVetoRelativeNsTime.clear();
    varVetoRelativePsTime.clear();

    for (auto &sig : *events)
    {

        varDetectorLUT.push_back(sig[0]->getDetectorLut());
        varDetectorType.push_back(sig[0]->getDetectorType());

        varEnergy.push_back(sig[0]->getEnergy());
        varPsd.push_back(sig[0]->getPSD());

        uint16_t fTime = sig[0]->getRelativeTime() / 1000;
        uint16_t sTime = sig[0]->getRelativeTime() % 1000;

        varRelativeNsTime.push_back(fTime);
        varRelativePsTime.push_back(sTime);

        if (sig.size() == 2)
        {
            varVetoEnergy.push_back(sig[1]->getEnergy());

            uint16_t fTime = sig[1]->getRelativeTime() / 1000;
            uint16_t sTime = sig[1]->getRelativeTime() % 1000;

            varVetoRelativeNsTime.push_back(fTime);
            varVetoRelativePsTime.push_back(sTime);
        }
        else
        {
            varVetoEnergy.push_back(0);
            varVetoRelativeNsTime.push_back(0);
            varVetoRelativePsTime.push_back(0);
        }
    }

    varEventTree->Fill();
}

void rootDataWriter::closeFile()
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

void capDataWriter::addBuiltEvent(vector<vector<event *>> *events, int64_t time)
{

    message.reset(new MallocMessageBuilder());

    auto outEvent = message->initRoot<BuiltEvent>();

    auto signals = outEvent.initSignals(events->size());

    vector<vector<event *>> twoEvents;
    vector<vector<event *>> fourEvents;

    for (size_t i = 0; i < events->size(); i++)
    {
        if (events->at(i).at(0)->getBoardNumb() == 4)
        {
            if (events->at(i).at(0)->getChannelNumb() >= 0 && events->at(i).at(0)->getChannelNumb() < 3)
            {
                if (events->at(i).at(0)->getEnergy() > 200)
                {
                    twoEvents.push_back({events->at(i).at(0)});
                }
            }
        }

        if (events->at(i).at(0)->getBoardNumb() == 2)
        {
            if (events->at(i).at(0)->getChannelNumb() >= 0 && events->at(i).at(0)->getChannelNumb() < 3)
            {
                if (events->at(i).at(0)->getEnergy() > 200)
                {
                    fourEvents.push_back({events->at(i).at(0)});
                }
            }
        }
        // auto sig = signals[i];

        // sig.setType(events->at(i).at(0)->getDetectorType());
        // sig.setLut(events->at(i).at(0)->getDetectorLut());

        // sig.setEnergy(events->at(i).at(0)->getEnergy());

        // sig.setTimestamp(events->at(i).at(0)->getTimeStamp());

        // uint16_t fTime = events->at(i).at(0)->getTimeStamp() / 1000;
        // uint16_t sTime = events->at(i).at(0)->getTimeStamp() % 1000;

        // sig.setRelativeNsTime(fTime);
        // sig.setRelativePsTime(sTime);

        // sig.initWaveform(events->at(i).at(0)->getWaveform().size());

        // sig.setWaveform(kj::arrayPtr(events->at(i).at(0)->getWaveform().data(), events->at(i).at(0)->getWaveform().size()));
    }

    writePackedMessageToFd(getFileDescriptor(), *message);
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

void gaspDataWriter::addBuiltEvent(vector<vector<event *>> *events, int64_t time)
{
    vector<int> eventMask = vector<int>(varOutputTypes.size(), 0);

    for (auto &sig : *events)
    {
        auto it = varOutputTypes.find(sig[0]->getDetectorType());

        auto dist = distance(varOutputTypes.begin(), it);

        eventMask[dist] += 1;
    }

    vector<short> eventBuffer;
    eventBuffer.reserve(2 + eventMask.size());

    eventBuffer.push_back(0);

    int size = 0;

    // size of header and detector mask
    size += varEventHeaderSize + varOutputTypes.size();

    // this should be from config
    if (time > (prevTime + resetGap))
    {
        resetTime = time;
    }

    prevTime = time;

    short scaledTime = (time - resetTime) * timeScale;

    // fill header parameters
    eventBuffer.push_back(scaledTime);

    // fill detector mask
    for (uint i = 0; i < eventMask.size(); i++)
    {
        auto it = std::next(varOutputTypes.begin(), i);

        size += eventMask[i] * detectorTypeSize[(*it).second];
        eventBuffer.push_back(eventMask[i]);
    }

    // fill the proper event start value
    eventBuffer[0] = 0xF000 + size;

    eventBuffer.reserve(size + 1);

    for (auto &sig : *events)
    {

        switch (varOutputTypes[sig[0]->getDetectorType()])
        {
        case DETECTOR:
        {
            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            uint16_t fTime = sig[0]->getRelativeTime() / 1000;
            uint16_t sTime = sig[0]->getRelativeTime() % 1000;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            break;
        }
        case DETECTOR_RAW:
        {

            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            int32_t time = sig[0]->getRelativeTime();

            uint16_t sTime = time & 0x3FF;
            uint16_t fTime = (time >> 10) & 0xFFFF;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            break;
        }
        case SUPPRESED_DETECTOR:
        {

            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            uint16_t fTime = sig[0]->getRelativeTime() / 1000;
            uint16_t sTime = sig[0]->getRelativeTime() % 1000;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            if (sig.size() != 2)
            {
                throw "error: Suppressed detector event must have two signals";
            }

            eventBuffer.push_back(sig[1]->getEnergy());

            uint16_t fVetoTime = sig[1]->getRelativeTime() / 1000;
            uint16_t sVetoTime = sig[1]->getRelativeTime() % 1000;

            eventBuffer.push_back(fVetoTime);
            eventBuffer.push_back(sVetoTime);
            break;
        }
        case SUPPRESED_DETECTOR_RAW:
        {

            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            int32_t time = sig[0]->getRelativeTime();

            uint16_t sTime = time & 0x3FF;
            uint16_t fTime = (time >> 10) & 0xFFFF;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            if (sig.size() != 2)
            {
                throw "error: Suppressed detector raw event must have two signals";
            }

            eventBuffer.push_back(sig[1]->getEnergy());

            time = sig[1]->getRelativeTime();

            sTime = time & 0x3FF;
            fTime = (time >> 10) & 0xFFFF;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            break;
        }
        case PSD_DETECTOR:
        {

            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            uint16_t fTime = sig[0]->getRelativeTime() / 1000;
            uint16_t sTime = sig[0]->getRelativeTime() % 1000;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            eventBuffer.push_back(sig[0]->getPSD());

            break;
        }
        case SUPPRESED_PSD_DETECTOR:
        {

            eventBuffer.push_back(sig[0]->getDetectorLut());
            eventBuffer.push_back(sig[0]->getEnergy());

            uint16_t fTime = sig[0]->getRelativeTime() / 1000;
            uint16_t sTime = sig[0]->getRelativeTime() % 1000;

            eventBuffer.push_back(fTime);
            eventBuffer.push_back(sTime);

            eventBuffer.push_back(sig[0]->getPSD());

            if (sig.size() != 2)
            {
                throw "error: Suppressed detector event must have two signals";
            }

            eventBuffer.push_back(sig[1]->getEnergy());

            uint16_t fVetoTime = sig[1]->getRelativeTime() / 1000;
            uint16_t sVetoTime = sig[1]->getRelativeTime() % 1000;

            eventBuffer.push_back(fVetoTime);
            eventBuffer.push_back(sVetoTime);
            break;
        }
        case NO_TYPE:
        {
            break;
        }
        default:
        {
            throw "default value, is not implemented: " + to_string(sig[0]->getDetectorType());
            break;
        }
        }
    }

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

void gaspDataWriter::sortByTypeAndLut(vector<event *> &events)
{
    // sort by (type and timestamp)
    auto start = events.begin();
    for (auto &val : sortTypePriority)
    {
        auto it = std::partition(start, events.end(), [val](event *ev)
                                 { return ev->getDetectorType() == val; });

        sort(start, it, [](event *i, event *m)
             { return i->getDetectorLut() < m->getDetectorLut(); });

        start = it;
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

    // lut getter
    lutValueGetter = [](event *ev)
    {
        return ev->getDetectorLut();
    };

    eventValueGetters = vector<function<uint16_t(event *)>>(valueGetters.size());

    // set the getter function
    for (auto &getter : valueGetters)
    {
        auto dist = std::distance(valueGetters.begin(), valueGetters.find(getter.first));
        eventValueGetters[dist] = getter.second;
    }
}

vector<int> gaspDataWriter::getValueGetterIdx(string format)
{
    auto dataTypes = varConfig["gasp"]["detectors"][format]["dataType"].get<vector<string>>();

    vector<int> dataTypeIdx = vector<int>(dataTypes.size());

    for (int i = 0; i < dataTypes.size(); i++)
    {
        auto dType = dataTypes[i];

        auto dist = std::distance(valueGetters.begin(), valueGetters.find(dType));
        dataTypeIdx[i] = dist;
    }

    return dataTypeIdx;
}

void gaspDataWriter::initializeWriterStructures()
{

    initializeValueGetters();

    auto cfg = varConfig["gasp"];

    auto outputFormat = cfg["typeOutputFormat"].get<vector<string>>();

    int cnt = 0;
    for (int i = 0; i < outputFormat.size(); i++)
    {
        auto dList = cfg["detectors"][outputFormat[i]]["type"].get<vector<int>>();

        valueGetterIdx[i] = getValueGetterIdx(outputFormat[i]);

        if (dList.size() == 0)
        {
            outputTypeMapper[i] = -1;
            continue;
        }

        outputTypeMapper[i] = cnt;

        outputTypes[cnt] = dList;

        cnt++;
    }

    sortTypePriority = vector<int>(outputTypeMapper.size());
    std::iota(sortTypePriority.begin(), sortTypePriority.end(), 0);

    auto pairs = varConfig["detector"]["coincPairs"].get<vector<vector<int>>>();
    auto types = varConfig["detector"]["type"].get<vector<vector<int>>>();
    auto luts = varConfig["detector"]["lut"].get<vector<vector<int>>>();

    for (size_t i = 0; i < pairs.size(); i++)
    {
        for (size_t o = 0; o < pairs[i].size(); o++)
        {
            auto pair = pairs[i][o];
            auto type = types[i][o];

            typePairToLut[{type, pair}] = luts[i][o];
        }
    }

    eventMaskSize = outputTypeMapper.size();

    auto findIt = find_if(outputTypeMapper.begin(), outputTypeMapper.end(), [](pair<int, int> pair)
                          { return pair.second == -1; });

    if (findIt != outputTypeMapper.end())
    {
        eventMaskSize--;
    }
}

event *gaspDataWriter::findEvent(int crtIdx, vector<event *> events, int reqType)
{

    auto evtPair = events.at(crtIdx)->getCoincPair();

    for (size_t i = 0; i < events.size(); i++)
    {
        auto evt = events.at(i);

        if (evt->getDetectorType() == reqType && evt->getCoincPair() == evtPair)
        {
            return evt;
        }
    }

    auto evt = new event();

    auto lut = typePairToLut[{reqType, evtPair}];

    evt->setDetectorType(reqType);
    evt->setDetectorLut(lut);

    evt->setEnergy(0);
    evt->setRelativeTime(0);
    evt->setPSD(0);

    return evt;
}

void gaspDataWriter::addBuiltEvent(vector<event *> &events)
{

    // sort by type a
    sortByTypeAndLut(events);

    vector<int> eventMask = vector<int>(eventMaskSize, 0);

    [[deprecated("prealocate memory")]] vector<short> eventBuffer;
    [[deprecated("add it to the buffer class")]] int size = 0;

    // add header
    eventBuffer.push_back(0); // size word
    eventBuffer.push_back(0); // time word

    size += 1; // the size word is not counted

    // add event mask locations
    for (size_t i = 0; i < eventMask.size(); i++)
    {
        eventBuffer.push_back(0);
    }

    size += eventMask.size();

    for (int i = 0; i < events.size(); i++)
    {
        auto crtEvent = events.at(i);

        auto evtType = events.at(i)->getDetectorType();
        auto outType = outputTypeMapper[evtType];

        if (outType == -1)
        {
            cout << "here" << endl;

            continue;
        }

        auto lutValue = lutValueGetter(crtEvent);

        eventBuffer.push_back(lutValue);

        size += 1;

        for (int o = 0; o < outputTypes[outType].size(); o++)
        {
            auto dtype = outputTypes[outType][o];
            auto getIdx = valueGetterIdx[evtType][o];

            [[ddeprecated("delete the generated one, but not the other")]] auto writeEvent = findEvent(i, events, dtype);

            auto value = eventValueGetters[getIdx](writeEvent);

            eventBuffer.push_back(value);
        }

        size += outputTypes[outType].size();

        eventMask[outType] += 1;
    }

    eventBuffer[0] = 0xF000 + size;

    [[deprecated]] int offset = 2; // this assumes header of 1, must be changed at some point

    for (size_t i = 0; i < eventMask.size(); i++)
    {
        eventBuffer[i + offset] = eventMask[i];
    }

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
