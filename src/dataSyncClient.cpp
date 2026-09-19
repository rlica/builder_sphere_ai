#include "dataSyncClient.h"
#include <filesystem>

namespace fs = std::filesystem;

mutex dataSyncClient::varMutex;
dataSyncClient *dataSyncClient::parInstance;

dataSyncClient::dataSyncClient()
{
    auto cfg = dataConfig::getInstance();

    string addr = "localhost:" + cfg->config["syncPort"].get<string>();

    client = make_shared<capnp::EzRpcClient>(addr);

    fs::path filePath(cfg->config["crtFile"]);

    filename = filePath.filename();
}
void dataSyncClient::updateState(tuple<int, string, double> state, bool force)
{
    auto cfg = dataConfig::getInstance();

    auto time = getCrtTime();

    if ((time - prevUpdateTime > 1e9) || force)
    {
        auto &waitScope = client->getWaitScope();

        // Get the "capability" for the remote StateSynchronizer interface
        StateSynchronizer::Client cap = client->getMain<StateSynchronizer>();

        // --- 1. Report 'initializing' state ---
        auto req = cap.reportStateRequest();
        req.getState().setProcId(get<0>(state));
        req.getState().setMaxId(cfg->config["maxId"]);

        req.getState().setOp(get<1>(state));
        req.getState().setProc(get<2>(state));
        req.getState().setTime((time - cfg->getStartTime()) * pow(10, -9));
        req.getState().setFilename(getFilename());

        auto promise = req.send();
        auto response = promise.wait(waitScope);

        prevUpdateTime = time;
    }
}

void dataSyncClient::startTracking(tbb::enumerable_thread_specific<atomic<uint64_t>> &crtVal, double maxVal)
{

    auto rpcClient = dataSyncClient::getInstance();
    auto cfg = dataConfig::getInstance();

    auto fileId = cfg->config["crtId"];

    trackStatus = true;

    trackFuture = async(std::launch::async, [fileId, &crtVal, maxVal, this]() -> void
                        { 
                                dataSyncClient::clearInstance();
                                auto rpcClient = dataSyncClient::getInstance();

                                rpcClient->updateState({fileId, "sorting data (2/4)", 0}, true);

                                auto crtTime = getCrtTime();

                            while (trackStatus){

                                    if(getCrtTime() - crtTime > 1e9){
                                        
                                        uint64_t total_sum = std::accumulate(crtVal.begin(), crtVal.end(), 0);  

                                        double proc = (total_sum * 150) / maxVal;

                                        if(proc > 100){
                                            proc = 100;
                                        }

                                        rpcClient->updateState({fileId, "sorting data (2/4)", proc});
                                        
                                        crtTime = getCrtTime();
                                    }
                            }
                        
                            rpcClient->updateState({fileId, "sorting data (2/4)", 100}, true); });
}

void dataSyncClient::stopTracking()
{
    trackStatus = false;
    trackFuture.get();

    dataSyncClient::clearInstance();
    auto rpcClient = dataSyncClient::getInstance();
}