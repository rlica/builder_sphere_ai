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

void gaspDataWriter::addBuiltEvent(vector<event *> *events, vector<int> &eventMask, int64_t time)
{

    // pop bgo from event mask
    eventMask.erase(eventMask.begin() + 2);

    // set a default 0 value for the start of the event
    vector<short> eventBuffer = vector<short>(1, 0);

    int size = 0;

    // size of header and detector mask
    size += varEventHeaderSize + varParamMask.size();

    // this should be from config
    if (time > (prevTime + resetGap))
    {
        resetTime = time;
    }

    prevTime = time;

    short scaledTime = (time - resetTime) * timeScale;

    // fill header parameters for now they are empty set abs time in 10s per sample
    eventBuffer.push_back(scaledTime);

    // fill detector mask
    for (uint i = 0; i < eventMask.size(); i++)
    {
        size += eventMask[i] * varParamMask[i];
        eventBuffer.push_back(eventMask[i]);
    }

    // fill the proper event start value
    eventBuffer[0] = 0xF000 + size;

    for (int i = 0; i < events->size(); i++)
    {
        switch (events->at(i)->getDetectorType())
        {
        case 0:

            eventBuffer.push_back(events->at(i)->getLutNumb());
            eventBuffer.push_back(events->at(i)->getEnergy());

            eventBuffer.push_back(events->at(i)->getRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getRelativeTimePs());

            eventBuffer.push_back(events->at(i)->getVetoEnergy());
            eventBuffer.push_back(events->at(i)->getVetoRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getVetoRelativeTimePs());
            break;

        case 1:

            eventBuffer.push_back(events->at(i)->getLutNumb());
            eventBuffer.push_back(events->at(i)->getEnergy());

            eventBuffer.push_back(events->at(i)->getRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getRelativeTimePs());

            eventBuffer.push_back(events->at(i)->getVetoEnergy());

            eventBuffer.push_back(events->at(i)->getVetoRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getVetoRelativeTimePs());
            break;

        case 2:

            cout << "i should not be here this time" << endl;
            abort();
            break;

        case 3:

            eventBuffer.push_back(events->at(i)->getLutNumb());
            eventBuffer.push_back(events->at(i)->getEnergy());

            eventBuffer.push_back(events->at(i)->getRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getRelativeTimePs());

            eventBuffer.push_back(events->at(i)->getPSD());

            break;

        case 4:

            eventBuffer.push_back(events->at(i)->getLutNumb());
            eventBuffer.push_back(events->at(i)->getEnergy());

            eventBuffer.push_back(events->at(i)->getRelativeTimeNs());
            eventBuffer.push_back(events->at(i)->getRelativeTimePs());

            break;

        default:

            cout << "default value, is not implemented: " << events->at(i)->getDetectorType() << endl;

            break;
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
    varEventTree->Branch("vetoEnergy", &varVetoEnergy);

    varEventTree->Branch("relativeNsTime", &varRelativeNsTime);
    varEventTree->Branch("relativePsTime", &varRelativePsTime);

    varEventTree->Branch("vetoRelativeNsTime", &varVetoRelativeNsTime);
    varEventTree->Branch("vetoRelativePsTime", &varVetoRelativePsTime);
}

void rootDataWriter::addBuiltEvent(vector<event *> *events, vector<int> &eventMask, int64_t time)
{

    varDetectorLUT.clear();
    varDetectorType.clear();

    varEnergy.clear();
    varVetoEnergy.clear();

    varRelativeNsTime.clear();
    varRelativePsTime.clear();

    varVetoRelativeNsTime.clear();
    varVetoRelativePsTime.clear();

    for (uint i = 0; i < events->size(); i++)
    {

        varDetectorLUT.push_back(events->at(i)->getLutNumb());
        varDetectorType.push_back(events->at(i)->getDetectorType());

        varEnergy.push_back(events->at(i)->getEnergy());
        varVetoEnergy.push_back(events->at(i)->getVetoEnergy());

        varRelativeNsTime.push_back(events->at(i)->getRelativeTimeNs());
        varRelativePsTime.push_back(events->at(i)->getRelativeTimePs());

        varVetoRelativeNsTime.push_back(events->at(i)->getVetoRelativeTimeNs());
        varVetoRelativePsTime.push_back(events->at(i)->getVetoRelativeTimePs());
    }

    varEventTree->Fill();
}

void rootDataWriter::closeFile()
{
    varEventTree->Write();
    varOutFile->Close();
}