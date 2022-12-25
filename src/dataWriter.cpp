#include "dataWriter.h"
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <regex>
#include <iomanip>

gaspDataWriter::gaspDataWriter(string file, int numb)
    : varFileName(file), varRunNumb(numb)

{
    initializeOutputFile("run");
    initializeRecordHeader();
}

void gaspDataWriter::addBuiltEvent(vector<event *> *events, vector<int> &eventMask, uint64_t time)
{

    // pop bgo from event mask
    eventMask.erase(eventMask.begin() + 2);

    // set a default 0 value for the start of the event
    vector<short> eventBuffer = vector<short>(1, 0);

    int size = 0;

    // size of header and detector mask
    size += varEventHeaderSize + varParamMask.size();

    // fill header parameters for now they are empty set abs time in 10s per sample
    eventBuffer.push_back(time * pow(10, -13));

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
    addByteWrite(varBlockBuffer.size() * sizeof(short));

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
    addByteWrite(varBlockBuffer.size() * sizeof(short));
}

void gaspDataWriter::closeFile()
{

    varBlockBuffer.insert(varBlockBuffer.end(), MAX_BLOCK_SIZE - varBlockBuffer.size(), 0);
    writeData();

    varOutFile.close();
    varOutFile.clear();
}

void gaspDataWriter::initializeOutputFile(string scope)
{

    resetBlockNumber();

    varOutFile.open(getFileName().c_str());

    initializeFileHeader();
}
