#pragma once

#include <memory>
#include <iostream>
#include <capnp/ez-rpc.h>
#include <kj/async-io.h>
#include "eventProto.capnp.h"
#include <mutex>
#include "dataConfig.h"
#include <atomic>
#include <future>
#include "tbb/enumerable_thread_specific.h"

using namespace std;

class dataSyncClient
{
public:
    dataSyncClient();

    ~dataSyncClient()
    {
    }

    static dataSyncClient *getInstance()
    {
        std::lock_guard<std::mutex> lock(varMutex);
        if (!parInstance)
        {
            parInstance = new dataSyncClient();
        }

        return parInstance;
    }

    static void clearInstance()
    {
        lock_guard<std::mutex> lock(varMutex);

        parInstance = nullptr;
    }

    void updateState(tuple<int, string, double> state, bool force = false);

    void startTracking(tbb::enumerable_thread_specific<atomic<uint64_t>> &crtVal, double maxVal);
    void stopTracking();

    string getFilename() { return filename; }

private:
    static dataSyncClient *parInstance;

    static std::mutex varMutex; // For locking when instance is created.

    shared_ptr<capnp::EzRpcClient> client;

    uint64_t prevUpdateTime = 0;

    bool trackStatus = false;
    future<void> trackFuture;

    string filename;
};