
#ifndef datawriter_h
#define datawriter_h

#include <deque>
#include <memory>
#include <vector>
#include "utils.h"
#include <fstream>
#include <math.h> /* isnan, sqrt */
#include "json.hpp"
#include <climits>
#include "TFile.h"
#include "TTree.h"

#define MAX_BLOCK_SIZE pow(2, 13)
#define EVENT_HEADER_SIZE pow(2, 4)

using namespace std;
using json = nlohmann::json;

class dataWriter
{
public:
    dataWriter(){};

    dataWriter(string file, int numb);
    ~dataWriter(){};

    virtual void initializeOutputFile(){};
    virtual void addBuiltEvent(vector<event *> *events, vector<int> &eventMask, int64_t time)
    {
        cout << "why i am here" << endl;
    };

    virtual void writeData(){};
    virtual void closeFile(){};

    virtual void setConfig(json config);

    string getFileName() { return varFileName; }
    int getRunNumber() { return varRunNumb; }

protected:
    int64_t resetGap = -1;
    double timeScale = 1;

    json varConfig;

    string varFileName;
    int varRunNumb;
};

class rootDataWriter : public dataWriter
{

public:
    rootDataWriter(string file, int numb);
    void initializeOutputFile();
    void addBuiltEvent(vector<event *> *events, vector<int> &eventMask, int64_t time);
    void closeFile();

    vector<uint16_t> varDetectorLUT;
    vector<uint16_t> varDetectorType;

    vector<uint16_t> varEnergy;
    vector<uint16_t> varVetoEnergy;

    vector<uint16_t> varRelativeNsTime;
    vector<uint16_t> varRelativePsTime;

    vector<uint16_t> varVetoRelativeNsTime;
    vector<uint16_t> varVetoRelativePsTime;

private:
    void initializeTree();

    unique_ptr<TFile> varOutFile;
    TTree *varEventTree;
};

class gaspDataWriter : public dataWriter
{

public:
    gaspDataWriter(string file, int numb);

    void initializeOutputFile();
    void addBuiltEvent(vector<event *> *events, vector<int> &eventMask, int64_t time);

    void writeData();
    void closeFile();

private:
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

    ofstream varOutFile;

    int64_t resetTime = std::numeric_limits<int64_t>::min();
    int64_t prevTime = std::numeric_limits<int64_t>::min();
};

#endif