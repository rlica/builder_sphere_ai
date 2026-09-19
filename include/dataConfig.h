#pragma once

#include "utils.h"
#include <mutex>

class dataConfig
{
public:
    dataConfig() {};

    void setConfig(json cfg)
    {
        std::lock_guard<std::mutex> lock(varMutex);

        config = cfg;

        startTime = getCrtTime();
    }

    static std::shared_ptr<dataConfig> getInstance()
    {
        std::lock_guard<std::mutex> lock(varMutex);
        if (!parInstance)
        {
            parInstance = std::make_shared<dataConfig>();
        }

        return parInstance;
    }

    static void clearInstance()
    {
        lock_guard<std::mutex> lock(varMutex);

        parInstance = nullptr;
    }

    uint64_t getStartTime()
    {
        return startTime;
    }

    json config;

private:
    static std::shared_ptr<dataConfig> parInstance;
    static std::mutex varMutex; // For locking when instance is created.

    uint64_t startTime;
};