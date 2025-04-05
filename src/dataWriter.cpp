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

    initializeValueGetters();

    initializeOutputFile();
    initializeRecordHeader();
}

void gaspDataWriter::initializeValueGetters()
{

    map<string, function<uint16_t(event *)>> valueGetters;

    valueGetters["energy"] = [](event *ev)
    {
        cout << " energy ";
        return ev->getEnergy();
    };

    valueGetters["time/1000"] = [](event *ev)
    {
        cout << " time/1000 ";
        return ev->getRelativeTime() / 1000;
    };

    valueGetters["time%1000"] = [](event *ev)
    {
        cout << " time%1000 ";
        return ev->getRelativeTime() % 1000;
    };

    valueGetters["psd"] = [](event *ev)
    {
        cout << " psd ";
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

    auto cfg = varConfig["gasp"];

    auto outputFormat = cfg["typeOutputFormat"].get<vector<string>>();

    for (size_t i = 0; i < outputFormat.size(); i++)
    {
        auto detType = outputFormat[i];

        detDataTypeVector[i] = vector<int>(0);
        for (auto &dataType : cfg["detectors"][detType]["dataType"])
        {
            auto dist = std::distance(valueGetters.begin(), valueGetters.find(dataType));
            detDataTypeVector[i].push_back(dist);
        }
    }
}

[[deprecated("not finished")]] void gaspDataWriter::validateConfig()
{
    // check detector values

    map<int, set<int>> detTypesSet;

    auto cfg = varConfig["gasp"];

    auto outputFormat = cfg["typeOutputFormat"].get<vector<string>>();

    int cnt = 0;

    for (auto &detType : outputFormat)
    {

        cout << "detType: " << detType << endl;

        if (detType == "noType")
        {
            cnt++;

            continue;
        }

        assert(cfg["detectors"].find(detType) != cfg["detectors"].end() && "gasp: detector type not defined");

        auto det = cfg["detectors"][detType];

        auto dataTypes = det["dataType"].get<vector<string>>();
        auto types = det["type"].get<vector<int>>();

        assert(dataTypes.size() == types.size() && "gasp: dataType and type vectors must be the same size");

        set<int> tSet;
        for (auto &tp : types)
        {
            tSet.insert(tp);
        }

        cout << "cnt:  " << cnt << " type: " << detType << endl;

        assert(tSet.find(cnt) != tSet.end() && "gasp: type not found in the defined config");

        detTypesSet[cnt] = tSet;
        detTypesVector[cnt] = types;

        cnt++;
    }

    // check pairs
    auto pairs = varConfig["detector"]["coincPairs"].get<vector<vector<int>>>();
    auto types = varConfig["detector"]["type"].get<vector<vector<int>>>();
    auto luts = varConfig["detector"]["lut"].get<vector<vector<int>>>();

    map<int, vector<int>> pairTypes;
    map<int, set<int>> pairSets;

    set<int> allTypes;

    for (size_t i = 0; i < pairs.size(); i++)
    {
        for (size_t o = 0; o < pairs[i].size(); o++)
        {
            allTypes.insert(types[i][o]);

            pairTypesLUTs[pairs[i][o]].push_back(make_pair(types[i][o], luts[i][o]));

            if (pairs[i][o] == 0)
            {
                continue;
            }

            if (types[i][o] == -1)
            {
                continue;
            }

            pairTypes[pairs[i][o]].push_back(types[i][o]);
            pairSets[pairs[i][o]].insert(types[i][o]);
        }
    }

    for (auto &pType : pairTypes)
    {
        bool found = false;

        for (auto &dType : detTypesSet)
        {
            if (pType.second.size() == dType.second.size())
            {
                if (pairSets[pType.first] == dType.second)
                {
                    found = true;
                    break;
                }
            }
        }

        assert(found && "gasp: invalid pair configuration");
    }

    maxType = *allTypes.rbegin() + 1;

    for (auto &dType : detTypesSet)
    {
        for (auto &type : dType.second)
        {
            detTypesUnique[dType.first].push_back(type);
        }
    }

    // create detector type map

    int typeCnt = 0;
    for (int i = 0; i < outputFormat.size(); i++)
    {
        auto detType = outputFormat[i];

        if (detType == "noType")
        {
            continue;
        }

        detTypeWriteMapper[i] = typeCnt;

        typeCnt++;
    }
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

void gaspDataWriter::addBuiltEvent(vector<event *> *events)
{

    [[deprecated("force the type to be in the pair, at check time")]] int a;
    [[deprecated("check if the pair has the same type, at check time")]] int c;

    // map with id and type - the type is required to generate the missing events
    map<pair<int, int>, vector<event *>> eventMap;

    vector<int> eventMask = vector<int>(detTypeWriteMapper.size(), 0);

    if (events->size() == 1)
    {
        return;
    }
    cout << "new event" << endl;
    for (size_t i = 0; i < events->size(); i++)
    {

        auto evtType = events->at(i)->getDetectorType();
        auto evtPair = events->at(i)->getCoincPair();

        auto dTypes = detTypesUnique[evtType];

        if (!dTypes.size())
        {
            continue;
        }

        auto maskId = detTypeWriteMapper[evtType];

        cout << "mask id: " << maskId << endl;

        [[deprecated("is i required?")]] pair<int, int> evtId = {i, evtType};

        // initialize the full event list
        eventMap[evtId] = vector<event *>(maxType, nullptr);

        // set the signal as starting point for the event
        eventMap[evtId][evtType] = events->at(i);

        // dont search for pairs if the event is not in a pair
        if (evtPair == 0)
        {
            continue;
        }

        // search for the pairs
        for (size_t o = 0; o < events->size(); o++)
        {

            // skip self signals
            if (i == o)
            {
                continue;
            }

            // skip signals that are not in the pair
            if (events->at(o)->getCoincPair() != evtPair)
            {
                continue;
            }

            // skip signals that are not in output
            if (find(dTypes.begin(), dTypes.end(), events->at(o)->getDetectorType()) == dTypes.end())
            {
                continue;
            }

            eventMap[evtId][events->at(o)->getDetectorType()] = events->at(o);
        }
    }

    // generate the missing events
    for (auto &evt : eventMap)
    {
        auto evtType = evt.first.second;
        auto dTypes = detTypesUnique[evtType];

        for (int i = 0; i < dTypes.size(); i++)
        {
            // skip the signals that are already in the event
            if (evt.second[dTypes[i]] != nullptr)
            {
                continue;
            }

            // find the lut of the event
            auto pairNumb = events->at(evt.first.first)->getCoincPair();

            auto pairType = pairTypesLUTs[pairNumb];

            uint16_t lut = 0;

            for (auto &pair : pairType)
            {
                if (pair.first == dTypes[i])
                {
                    lut = pair.second;
                    break;
                }
            }

            // create the missing event
            evt.second[dTypes[i]] = new event();
            evt.second[dTypes[i]]->setDetectorType(dTypes[i]);
            evt.second[dTypes[i]]->setDetectorLut(lut);

            evt.second[dTypes[i]]->setEnergy(0);
            evt.second[dTypes[i]]->setRelativeTime(0);
            evt.second[dTypes[i]]->setPSD(0);
        }
    }

    dumpEventsToFile(eventMap);

    // call the getter
    // dump the event to file
}

void gaspDataWriter::dumpEventsToFile(map<pair<int, int>, vector<event *>> &eventMap)
{
    for (auto &evt : eventMap)
    {
        auto detTypes = detTypesVector[evt.first.second];
        auto dataTypes = detDataTypeVector[evt.first.second];

        auto getter = eventValueGetters[evt.first.second];

        for (size_t i = 0; i < detTypes.size(); i++)
        {
            auto dt = detTypes[i];
            auto data = dataTypes[i];
            cout << dt << " " << data << " ";

            auto value = eventValueGetters[data](evt.second[dt]);

            cout << " value: " << value << endl;
        }
    }
}

// { // build events with anticoincidence
//     map<uint8_t, deque<event *>> coincMap;

//     for (uint i = 0; i < tempList.size(); i++)
//     {
//         coincMap[tempList[i]->getCoincPair()].push_back(tempList[i]);
//     }

//     for (auto &val : coincMap)
//     {

//         // the zero mappings do not have pairs
//         if (val.first != 0)
//         {
//             // one of the signals is missing
//             if (val.second.size() == 1)
//             {
//                 auto evt = new event();

//                 auto [type, lut] = val.second[0]->getCoincPairType();

//                 evt->setDetectorType(type);
//                 evt->setDetectorLut(lut);

//                 evt->setEnergy(0);
//                 evt->setRelativeTime(0);
//                 evt->setPSD(0);

//                 if (val.second[0]->getAntiCoincFlag()) // first signal is the anti-coinc flag -> push front
//                 {
//                     val.second.push_front(evt);
//                 }
//                 else // second signal is the anti-coinc flag -> push back
//                 {
//                     val.second.push_back(evt);
//                 }
//             }
//             else if (val.second.size() == 2) // both signals are here but the order might be wrong
//             {
//                 if (val.second[0]->getAntiCoincFlag()) // first signal is the anticoinc flag -> reverse
//                 {
//                     std::reverse(val.second.begin(), val.second.end());
//                 }
//             }
//             else
//             {
//                 throw "more then two coincident signals found!";
//             }
//         }
//     }

//     vector<vector<event *>> eventList;

//     for (auto &val : coincMap)
//     {
//         if (val.first == 0)
//         {
//             for (auto &evt : val.second)
//             {
//                 eventList.push_back(vector<event *>(1, evt));
//             }
//         }
//         else
//         {
//             eventList.push_back(vector<event *>(val.second.begin(), val.second.end()));
//         }
//     }
//     // build events with anticoincidence

//     // sort by type
//     std::sort(eventList.begin(), eventList.end(), [](auto &i, auto &j)
//               { return (i[0]->getDetectorType() < j[0]->getDetectorType()); });
// }
