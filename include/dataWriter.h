#pragma once

#include <deque>
#include <memory>
#include <vector>
#include "utils.h"
#include <fstream>
#include <math.h>
#include "json.hpp"
#include <climits>
#include "TFile.h"
#include "TTree.h"
#include "TH1I.h"
#include <fcntl.h>
#include "TParameter.h"
#include <limits>

#include "eventProto.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include "utilities.h"
#include <sys/file.h>

#define MAX_BLOCK_SIZE pow(2, 13)
#define EVENT_HEADER_SIZE pow(2, 4)

#define DETECTOR 0
#define SUPPRESED_DETECTOR 1
#define PSD_DETECTOR 2
#define SUPPRESED_PSD_DETECTOR 3
#define NO_TYPE 4
#define DETECTOR_RAW 5
#define SUPPRESED_DETECTOR_RAW 6

using namespace std;
using json = nlohmann::json;
using namespace capnp;

class dataWriter
{
public:
    dataWriter(json &config);

    ~dataWriter() {};

    virtual void initializeOutputFile() {};
    virtual void addBuiltEvent(vector<vector<event *>> *events, int64_t time) {};
    virtual void addBuiltEvent(vector<event *> &events) {};
    virtual void addConvertEvent(event *event) {};

    virtual void writeData() {};
    virtual void closeFile() {};

    virtual void writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist) {};
    virtual void writeAlignResults(vector<calibPoint> calibPoints) {};

    virtual void setConfig(json &config) {};

    string getFileName() { return varFileName; }
    int getRunNumber() { return varRunNumb; }

protected:
    int64_t resetGap = -1;
    double timeScale = 1;

    json varConfig;

    string varFileName = "";
    int varRunNumb = -1;

    uint64_t startTime = std::numeric_limits<uint64_t>::max();
    uint64_t stopTime = std::numeric_limits<uint64_t>::min();
};

class rootDataWriter : public dataWriter
{

public:
    rootDataWriter(json &config);

    void initializeBuildOutputFile();
    void initializeConvertOutputFile();
    void addConvertEvent(event *event);

    void addBuiltEvent(vector<vector<event *>> *events, int64_t time);

    void writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist);

    void closeFile();

    void setConfig(json &config);

private:
    void initializeBuildTree();
    void initializeConvertTree();

    unique_ptr<TFile> varOutFile;
    TTree *varEventTree;

    // build data types
    vector<uint16_t> varDetectorLUT;
    vector<uint16_t> varDetectorType;

    vector<uint16_t> varEnergy;

    vector<uint16_t> varPsd;

    vector<uint16_t> varVetoEnergy;

    vector<uint16_t> varRelativeNsTime;
    vector<uint16_t> varRelativePsTime;

    vector<uint16_t> varVetoRelativeNsTime;
    vector<uint16_t> varVetoRelativePsTime;

    // convert data types
    uint8_t varConvBoard;
    uint8_t varConvChannel;

    uint16_t varConvEnergy;
    uint64_t varConvTimestamp;

    map<pair<int, int>, shared_ptr<TH1D>> varEnergyHist;

    // flags
    bool varAddHistogram;
    bool varAddList;
    bool varAddRunTime;
};

class gaspDataWriter : public dataWriter
{

public:
    gaspDataWriter(json &config);

    void initializeOutputFile();
    void addBuiltEvent(vector<vector<event *>> *events, int64_t time);
    void addBuiltEvent(vector<event *> &events);

    void writeData();
    void writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist);

    void closeFile();

    void setConfig(json &config);
    void writeAlignResults(vector<calibPoint> calibPoints);

private:
    void initializeWriterStructures();
    void initializeValueGetters();

    void sortByTypeAndLut(vector<event *> &events);
    vector<int> getValueGetterIdx(string format);

    event *findEvent(event *crtEvent, vector<event *> events, int reqType);
    optional<event *> generateMainEvent(event *crtEvent, vector<event *> events);

    map<int, int> outputTypeMapper;

    map<int, vector<int>> outputTypes;
    map<int, vector<int>> valueGetterIdx;

    map<pair<int, int>, int> typePairToLut;
    map<pair<int, int>, pair<int, int>> mainEventType; // key: type and pair - to main type, lut

    vector<int> sortTypePriority;

    vector<function<uint16_t(event *)>> eventValueGetters; // faster retrieval
    map<string, function<uint16_t(event *)>> valueGetters; // string - function

    function<uint16_t(event *)> lutValueGetter;
    int eventMaskSize = 0;

private:
    void validateConfig();
    void initializeRecordHeader();
    void initializeFileHeader();

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

    [[deprecated]] map<int, int> detectorTypeSize = {{DETECTOR, 4},
                                                     {DETECTOR_RAW, 4},
                                                     {SUPPRESED_DETECTOR, 7},
                                                     {SUPPRESED_DETECTOR_RAW, 7},
                                                     {PSD_DETECTOR, 5},
                                                     {SUPPRESED_PSD_DETECTOR, 8},
                                                     {NO_TYPE, 0}};

    [[deprecated]] map<int, int> varOutputTypes; // detector type - gasp output type
};

class capDataWriter : public dataWriter
{

public:
    capDataWriter(json &config);

    void initializeOutputFile();

    void setFileDescriptor(int fd) { fileDescriptor = fd; }
    int getFileDescriptor() { return fileDescriptor; }

    void addBuiltEvent(vector<vector<event *>> *events, int64_t time);

    void writeDelayResults(map<pair<int, int>, shared_ptr<TH1I>> &delayHist);

    void closeFile();

private:
    int fileDescriptor;
    std::shared_ptr<MallocMessageBuilder> message;
};