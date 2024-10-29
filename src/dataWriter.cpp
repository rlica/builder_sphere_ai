#include "dataWriter.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <iomanip>

dataWriter::dataWriter(string file, int numb) : varFileName(file), varRunNumb(numb)
{
}

gaspDataWriter::gaspDataWriter(string file, int numb) : dataWriter(file, numb)
{

    initializeOutputFile();
    initializeRecordHeader();
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
                cout << "error: Suppressed detector event must have two signals" << endl;
                exit(1);
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
                cout << "error: Suppressed detector raw event must have two signals" << endl;
                exit(1);
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
                cout << "error: Suppressed detector event must have two signals" << endl;
                exit(1);
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

            cout << "default value, is not implemented: " << sig[0]->getDetectorType() << endl;

            abort();

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

void dataWriter::setConfig(json config)
{
    varConfig = config;

    resetGap = int(varConfig["build"]["absoluteTime"]["resetGap"]) * pow(10, 12);
    timeScale = pow(10, int(varConfig["build"]["absoluteTime"]["scalePower"]));
}

void gaspDataWriter::setConfig(json config)
{
    varConfig = config;

    resetGap = int(varConfig["build"]["absoluteTime"]["resetGap"]) * pow(10, 12);
    timeScale = pow(10, int(varConfig["build"]["absoluteTime"]["scalePower"]));

    auto dTypes = config["detector"]["type"].get<vector<vector<int>>>();

    map<int, int> typeCount;

    for (auto brd : dTypes)
    {
        for (auto ch : brd)
        {
            typeCount[ch]++;
        }
    }
    typeCount.erase(-1);

    auto outTypes = varConfig["gasp"]["typeOutputFormat"].get<vector<string>>();

    if (outTypes.size() < typeCount.size())
    {
        cout << "gasp typeOutputFormat are not set for all detector types" << endl;
        abort();
    }

    for (auto [type, count] : typeCount)
    {

        if (outTypes[type] == "detector")
        {
            varOutputTypes[type] = DETECTOR;
        }
        else if (outTypes[type] == "detectorRaw")
        {
            varOutputTypes[type] = DETECTOR_RAW;
        }
        else if (outTypes[type] == "suppressedDetector")
        {
            varOutputTypes[type] = SUPPRESED_DETECTOR;
        }
        else if (outTypes[type] == "suppressedDetectorRaw")
        {
            varOutputTypes[type] = SUPPRESED_DETECTOR_RAW;
        }
        else if (outTypes[type] == "psdDetector")
        {
            varOutputTypes[type] = PSD_DETECTOR;
        }
        else if (outTypes[type] == "suppresedPsdDetector")
        {
            varOutputTypes[type] = SUPPRESED_PSD_DETECTOR;
        }
        else
        {
            varOutputTypes[type] = NO_TYPE;
        }
    }

    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    for (auto m : antiCoincFlags)
    {
        varOutputTypes.erase(m);
    }
}

rootDataWriter::rootDataWriter(string file, int numb) : dataWriter(file, numb)
{
    initializeOutputFile();
}

void rootDataWriter::initializeOutputFile()
{

    string fName = getFileName() + ".root";

    varOutFile = make_unique<TFile>(fName.c_str(), "RECREATE");

    initializeTree();
}

void rootDataWriter::initializeTree()
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
    varEventTree->Write();
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

    cout << "results output: " << outFileName << endl;
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

    cout << "results output: " << outFileBase << "_#type" << endl;
}