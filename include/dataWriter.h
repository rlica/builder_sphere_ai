
#ifndef datawriter_h
#define datawriter_h

#include <deque>
#include <memory>
#include <vector>
#include "utils.h"
#include <fstream>
#include <math.h> /* isnan, sqrt */
#include "json.hpp"

#define MAX_BLOCK_SIZE pow(2, 13)
#define EVENT_HEADER_SIZE pow(2, 4)

using namespace std;
using json = nlohmann::json;

class gaspDataWriter
{

public:
    gaspDataWriter(string file, int numb);

    void initializeOutputFile(string scope);
    void addBuiltEvent(vector<event *> *events, vector<int> &eventMask, uint64_t time);

    void writeData();
    void closeFile();

    int getRunNumber() { return varRunNumb; }

    void setFileName(string name) { varFileName = name; }
    string getFileName() { return varFileName; }

    void setConfig(json config) { varConfig = config; }

    void addByteWrite(uint32_t size) { varByteWrite += size; }
    uint64_t getByteWrite();

private:
    uint64_t varByteWrite = 0;

    void initializeRecordHeader();
    void initializeFileHeader();

    // vector<int> varParamMask = {6, 7, 3, 4, 4};
    vector<int> varParamMask = {7, 7, 5, 4};

    vector<short> varBlockBuffer;

    int varEventHeaderSize = 1;

    void resetBlockNumber() { varBlockNumber = 0; }
    void incrementBlockNumber() { varBlockNumber++; }
    int getBlockNumber() { return varBlockNumber; }

    int getOutFileSize() { return getBlockNumber() * MAX_BLOCK_SIZE * sizeof(short); }

    int varBlockNumber = 0;
    json varConfig;

    int varRunNumb;
    string varFileName;

    ofstream varOutFile;
};
#endif