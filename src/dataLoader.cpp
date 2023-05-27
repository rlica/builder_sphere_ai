#include "dataLoader.h"
#include <random>

deque<event *> rootLoader::readEvents()
{

    vector<vector<int>> boards = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> channels = vector<vector<int>>(5, vector<int>(50, -100));
    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);

    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    map<pair<int, int>, pair<int, int>> coincPairTypes;

    uint16_t lut;
    uint16_t dType;
    uint16_t energy;
    ULong64_t timeStamp;
    double psd;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("events");

    chain->SetBranchAddress("energy", &energy);
    chain->SetBranchAddress("boardTimeStamp", &timeStamp);
    chain->SetBranchAddress("detectorLUT", &lut);
    chain->SetBranchAddress("detectorType", &dType);
    chain->SetBranchAddress("psd", &psd);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        auto board = boards[dType][lut];
        auto ch = channels[dType][lut];

        if (board == -100)
        {

            auto boardChannel = findBoardChannel(dType, lut);

            board = boards[dType][lut] = get<0>(boardChannel);
            ch = channels[dType][lut] = get<1>(boardChannel);
        }

        auto delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event());

        events.back()->setDetectorType(dType);
        events.back()->setEnergy(energy);
        events.back()->setTimeStamp(delayedTime);
        events.back()->setBoardNumb(board);
        events.back()->setChannelNumb(ch);

        events.back()->setLutNumb(lut);
        events.back()->setCoincPair(coincPairs[board][ch]);

        events.back()->setPSD(psd * 10000);

        auto findPair = make_pair(board, ch);

        if (!coincPairTypes.count(findPair))
        {
            coincPairTypes[findPair] = findCoincPairType(board, ch);
        }

        events.back()->setCoincPairType(coincPairTypes[findPair]);

        if (find(antiCoincFlags.begin(), antiCoincFlags.end(), events.back()->getDetectorType()) != antiCoincFlags.end())
        {
            events.back()->setAntiCoincFlag(true);
        }
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

deque<event *> protoLoader::readEvents()
{

    deque<event *> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    string fileName = config["crtFile"];

    auto fd = open(fileName.c_str(), O_RDONLY);

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPair = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    map<pair<int, int>, pair<int, int>> coincPairTypes;
    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    kj::FdInputStream fdStream(fd);
    kj::BufferedInputStreamWrapper bufferedStream(fdStream);

    while (bufferedStream.tryGetReadBuffer() != nullptr)
    {
        capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

        auto dts = message.getRoot<Dataset>();

        auto proEvent = dts.getEvents();

        for (uint i = 0; i < proEvent.size(); i++)
        {

            auto ev = new event();

            auto brd = proEvent[i].getBoard();
            auto ch = proEvent[i].getChannel();

            ev->setBoardNumb(brd);
            ev->setChannelNumb(ch);
            ev->setLutNumb(lut[brd][ch]);
            ev->setDetectorType(mask[brd][ch]);

            ev->setEnergy(proEvent[i].getEnergy());
            ev->setPSD(proEvent[i].getPsd() * 10000);

            uint64_t delayedTime = proEvent[i].getTimestamp() - delay[brd][ch];
            ev->setTimeStamp(delayedTime);

            // coinc
            ev->setCoincPair(coincPair[brd][ch]);

            auto findPair = make_pair(brd, ch);

            if (!coincPairTypes.count(findPair))
            {
                coincPairTypes[findPair] = findCoincPairType(brd, ch);
            }

            ev->setCoincPairType(coincPairTypes[findPair]);

            if (find(antiCoincFlags.begin(), antiCoincFlags.end(), ev->getDetectorType()) != antiCoincFlags.end())
            {
                ev->setAntiCoincFlag(true);
            }

            events.push_back(ev);

            if (events.size() == eventNumb)
            {
                return events;
            }
        }
    }

    return events;
}

deque<event *> delilaLoader::readEvents()
{

    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);

    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    map<pair<int, int>, pair<int, int>> coincPairTypes;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("ELIADE_Tree");

    UChar_t Mod;
    UChar_t Ch;
    uint16_t energy;
    ULong64_t timeStamp;
    double fineTs;

    chain->SetBranchAddress("Mod", &Mod);
    chain->SetBranchAddress("Ch", &Ch);
    chain->SetBranchAddress("ChargeLong", &energy);
    chain->SetBranchAddress("TimeStamp", &timeStamp);
    chain->SetBranchAddress("FineTS", &fineTs);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        int board = Mod;
        int ch = Ch;

        auto lut = config["detector"]["lut"][board][ch];
        auto dType = config["detector"]["type"][board][ch];

        uint64_t delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event());

        events.back()->setDetectorType(dType);
        events.back()->setEnergy(energy);
        events.back()->setTimeStamp(delayedTime);
        events.back()->setBoardNumb(board);
        events.back()->setChannelNumb(ch);

        events.back()->setLutNumb(lut);
        events.back()->setCoincPair(coincPairs[board][ch]);

        auto findPair = make_pair(board, ch);

        if (!coincPairTypes.count(findPair))
        {
            coincPairTypes[findPair] = findCoincPairType(board, ch);
        }

        events.back()->setCoincPairType(coincPairTypes[findPair]);

        if (find(antiCoincFlags.begin(), antiCoincFlags.end(), events.back()->getDetectorType()) != antiCoincFlags.end())
        {
            events.back()->setAntiCoincFlag(true);
        }
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

deque<event *> threeMVLoader::readEvents()
{

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPairs = config["detector"]["coincPairs"].get<vector<vector<int>>>();

    uint16_t ch;
    uint16_t energy;
    ULong64_t timeStamp;
    double psd;

    string fileName = config["crtFile"];

    auto tFile = new TFile(fileName.c_str());

    auto chain = (TTree *)tFile->Get("events");

    chain->SetBranchAddress("energy", &energy);
    chain->SetBranchAddress("boardTimeStamp", &timeStamp);
    chain->SetBranchAddress("detectorId", &ch);
    chain->SetBranchAddress("psd", &psd);

    deque<event *> events;

    int eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain->GetEntries();
    }

    int board = 0;
    int entry = 0;

    for (int i = 0; i < eventNumb; i++)
    {
        chain->GetEntry(i);

        auto delayedTime = timeStamp - delay[board][ch];

        events.push_back(new event());

        events.back()->setDetectorType(mask[board][ch]);
        events.back()->setEnergy(energy);
        events.back()->setTimeStamp(delayedTime);
        events.back()->setBoardNumb(board);
        events.back()->setChannelNumb(ch);

        events.back()->setLutNumb(lut[board][ch]);

        events.back()->setPSD(psd);
    }

    chain->Reset();
    delete chain;

    tFile->Close();
    delete tFile;

    return events;
}

deque<event *> capLoader::readEvents()
{

    deque<event *> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    string fileName = config["crtFile"];

    auto fd = open(fileName.c_str(), O_RDONLY);

    vector<vector<int>> delay = getDelayVector(config["detector"]["delayNs"]);
    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();
    vector<vector<int>> coincPair = config["detector"]["coincPairs"].get<vector<vector<int>>>();
    map<pair<int, int>, pair<int, int>> coincPairTypes;
    vector<int> antiCoincFlags = config["build"]["antiCoincFlag"].get<vector<int>>();

    kj::FdInputStream fdStream(fd);
    kj::BufferedInputStreamWrapper bufferedStream(fdStream);

    while (bufferedStream.tryGetReadBuffer() != nullptr)
    {
        capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

        dataReader dts = message.getRoot<PlainData>();

        int evtType = get<PlainData::Reader>(dts).getType();

        switch (evtType)
        {
        case 0:
            break;
        case 1:
            dts = message.getRoot<PsdData>();
            break;
        case 5:
            dts = message.getRoot<RawTimeData>();
            break;
        default:
            cout << "Unsupported data type: " << dataTypeMap[evtType] << endl;
            abort();
            break;
        }

        auto done = visit([&](auto &data) -> bool
                          {

                        auto proEvent = data.getEvents();

                        for (uint i = 0; i < proEvent.size(); i++){


                                   using T = decay_t<decltype(proEvent[i])>;

                                    auto ev = new event();

                                    auto brd = proEvent[i].getBoard();
                                    auto ch = proEvent[i].getChannel();

                                    ev->setBoardNumb(brd);
                                    ev->setChannelNumb(ch);
                                    ev->setLutNumb(lut[brd][ch]);
                                    ev->setDetectorType(mask[brd][ch]);

                                    ev->setEnergy(proEvent[i].getEnergy());

                                    if constexpr (is_same_v<T, PsdEvent>){
                                        ev->setPSD(proEvent[i].getPsd() * 10000);
                                    } else {
                                        ev->setPSD(0);
                                    }

                                    if constexpr (is_same_v<T,  RawTimeEvent::Reader>){   

                                        uint16_t fineTs = proEvent[i].getFineTimestamp() * 1000 / 1024;
                                        uint64_t ts = proEvent[i].getTimestamp() * 1000 + fineTs ;

                                        uint64_t delayedTime = ts - delay[brd][ch];

                                        ev->setRawTs(proEvent[i].getTimestamp());
                                        ev->setRawFineTs(proEvent[i].getFineTimestamp());
                                        
                                        ev->setTimeStamp(delayedTime);

                                    } else {
                                        
                                        uint64_t delayedTime = proEvent[i].getTimestamp() - delay[brd][ch];
                                        ev->setTimeStamp(delayedTime);
                                        
                                    }


                                    // coinc
                                    ev->setCoincPair(coincPair[brd][ch]);

                                    auto findPair = make_pair(brd, ch);

                                    if (!coincPairTypes.count(findPair))
                                    {
                                        coincPairTypes[findPair] = findCoincPairType(brd, ch);
                                    }

                                    ev->setCoincPairType(coincPairTypes[findPair]);

                                    if (find(antiCoincFlags.begin(), antiCoincFlags.end(), ev->getDetectorType()) != antiCoincFlags.end())
                                    {
                                        ev->setAntiCoincFlag(true);
                                    }

                                    events.push_back(ev);

                                    if (events.size() == eventNumb)
                                    {
                                        return true;
                                    }
                                }
                                
                      return false; },
                          dts);

        if (done)
        {
            return events;
        }
    }

    return events;
}

vector<delayEvent> rootLoader::readDelayEvents()
{
    vector<delayEvent> events;

    auto files = config["crtFile"].get<vector<string>>();

    TChain chain("events");

    for (auto file : files)
    {
        chain.Add(file.c_str());
    }

    uint16_t dLut;
    uint16_t dType;
    uint16_t energy;
    ULong64_t timeStamp;

    chain.SetBranchAddress("energy", &energy);
    chain.SetBranchAddress("boardTimeStamp", &timeStamp);
    chain.SetBranchAddress("detectorLUT", &dLut);
    chain.SetBranchAddress("detectorType", &dType);

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain.GetEntries();
    }

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (int i = 0; i < eventNumb; i++)
    {
        chain.GetEntry(i);

        if (energy > tresh)
        {

            if (dType == refType && dLut == refLUT)
            {
                timeStamp += offset;
            }

            events.push_back(make_tuple(dType, dLut, timeStamp));
        }
    }

    return events;
}

vector<delayEvent> protoLoader::readDelayEvents()
{
    vector<delayEvent> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (auto fileName : files)
    {

        auto fd = open(fileName.c_str(), O_RDONLY);

        kj::FdInputStream fdStream(fd);
        kj::BufferedInputStreamWrapper bufferedStream(fdStream);

        while (bufferedStream.tryGetReadBuffer() != nullptr)
        {
            capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

            auto dts = message.getRoot<Dataset>();

            auto proEvent = dts.getEvents();

            for (uint i = 0; i < proEvent.size(); i++)
            {

                auto brd = proEvent[i].getBoard();
                auto ch = proEvent[i].getChannel();

                auto dType = mask[brd][ch];
                auto dLut = lut[brd][ch];

                if (proEvent[i].getEnergy() > tresh)
                {

                    auto timeStamp = proEvent[i].getTimestamp();

                    if (dType == refType && dLut == refLUT)
                    {
                        timeStamp += offset;
                    }

                    events.push_back(make_tuple(dType, dLut, timeStamp));

                    if (events.size() == eventNumb)
                    {
                        return events;
                    }
                }
            }
        }
    }

    return events;
}

vector<delayEvent> delilaLoader::readDelayEvents()
{
    cout << "delila delay calculation not yet implemented" << endl;
    abort();
    return {};
}

vector<delayEvent> threeMVLoader::readDelayEvents()
{
    vector<delayEvent> events;

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    TChain chain("events");

    for (auto file : files)
    {
        chain.Add(file.c_str());
    }

    uint16_t ch;
    uint16_t energy;
    ULong64_t timeStamp;

    chain.SetBranchAddress("energy", &energy);
    chain.SetBranchAddress("boardTimeStamp", &timeStamp);
    chain.SetBranchAddress("detectorId", &ch);

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = chain.GetEntries();
    }

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];
    int tresh = config["delay"]["energyThreshold"];
    int offset = config["delay"]["refTimeOffset"];

    for (int i = 0; i < eventNumb; i++)
    {
        chain.GetEntry(i);

        if (energy > tresh)
        {

            auto brd = 0;

            auto dType = mask[brd][ch];
            auto dLut = lut[brd][ch];

            if (dType == refType && dLut == refLUT)
            {
                timeStamp += offset;
            }

            events.push_back(make_tuple(dType, dLut, timeStamp));
        }
    }

    return events;
}

vector<delayEvent> capLoader::readDelayEvents()
{

    vector<tuple<int, int, uint64_t>> events;

    uint eventNumb = config["input"]["eventCount"];

    if (eventNumb == 0)
    {
        eventNumb = numeric_limits<uint>::max();
    }

    auto files = config["crtFile"].get<vector<string>>();

    vector<vector<int>> lut = config["detector"]["lut"].get<vector<vector<int>>>();
    vector<vector<int>> mask = config["detector"]["type"].get<vector<vector<int>>>();

    int refLUT = config["delay"]["refLUT"];
    int refType = config["delay"]["refType"];

    int tresh = config["delay"]["energyThreshold"];

    int offset = config["delay"]["refTimeOffset"];

    for (auto fileName : files)
    {

        auto fd = open(fileName.c_str(), O_RDONLY);

        kj::FdInputStream fdStream(fd);
        kj::BufferedInputStreamWrapper bufferedStream(fdStream);

        while (bufferedStream.tryGetReadBuffer() != nullptr)
        {
            capnp::PackedMessageReader message(bufferedStream, {100000000, 64});

            dataReader dts = message.getRoot<PlainData>();

            int evtType;

            visit([&](auto &data)
                  { 
                    
                   switch(data.getType()){
                    case 0:
                        break;

                    case 1:
                        dts = message.getRoot<PsdData>();
                        break;

                    case 5:
                        dts = message.getRoot<RawTimeData>();
                        break;

                    default:
                        cout << "Unsupported data type: " << dataTypeMap[evtType] << endl;
                        abort();
                        break;

                   } },

                  dts);

            auto done = visit([&](auto &data) -> bool
                              {
                      auto proEvent = data.getEvents();

                      for (uint i = 0; i < proEvent.size(); i++)
                      {
                        
                          using T = decay_t<decltype(proEvent[i])>;

                          auto brd = proEvent[i].getBoard();
                          auto ch = proEvent[i].getChannel();

                          auto dType = mask[brd][ch];
                          auto dLut = lut[brd][ch];

                          if (proEvent[i].getEnergy() > tresh)
                          {

                             uint64_t timeStamp = 0;

                                if constexpr (is_same_v<T,  RawTimeEvent::Reader>){     

                                   uint16_t fineTs = proEvent[i].getFineTimestamp() * 1000 / 1024;
                                   timeStamp = proEvent[i].getTimestamp() * 1000 + fineTs ;

                               } else {                            
                                   timeStamp = proEvent[i].getTimestamp();
                               }

                              if (dType == refType && dLut == refLUT)
                              {
                                  timeStamp += offset;
                              }
                
                              events.push_back(make_tuple(dType, dLut, timeStamp));

                              if (events.size() == eventNumb)
                              {
                                  return true;
                              }
                          }
                      }
                      
                      return false; },
                              dts);

            if (done)
            {
                return events;
            }
        }
    }
    return events;
}

vector<vector<int>> dataLoader::getDelayVector(json delay)
{
    vector<vector<int>> tempDelay = delay.get<vector<vector<int>>>();

    for (auto &board : tempDelay)
    {
        for (auto &ch : board)
        {
            ch = ch * 1000;
        }
    }

    return tempDelay;
}

pair<int, int> dataLoader::findBoardChannel(int detType, int lut)
{

    vector<vector<int>> lutVect = config["detector"]["lut"];
    vector<vector<int>> bTypeVect = config["detector"]["type"];

    for (int i = 0; i < lutVect.size(); i++)
    {
        for (int o = 0; o < lutVect[0].size(); o++)
        {
            if (lutVect[i][o] == lut && bTypeVect[i][o] == detType)
            {
                return make_pair(i, o);
            }
        }
    }

    cout << "Board and channel not found for detType: " << detType << " and lut: " << lut << endl;

    abort();

    return {-1, -1};
}

pair<int, int> dataLoader::findCoincPairType(int brd, int ch)
{

    int foundBrd = 0;

    int pair = config["detector"]["coincPairs"][brd][ch];

    for (auto i : config["detector"]["coincPairs"])
    {
        auto pos = find(i.begin(), i.end(), pair);

        if (pos != i.end())
        {

            if (brd != foundBrd || ch != distance(i.begin(), pos))
            {

                auto p = make_pair(config["detector"]["type"][foundBrd][distance(i.begin(), pos)],
                                   config["detector"]["lut"][foundBrd][distance(i.begin(), pos)]);

                return p;
            }
        }

        foundBrd += 1;
    }

    return make_pair(0, -100);
}