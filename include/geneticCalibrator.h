#pragma once

#include <iostream>
#include "json.hpp"
#include <cassert>
#include "utilities.h"
#include <map>
#include "math.h"
#include <random>
#include <algorithm>
#include <filesystem>

constexpr uint8_t ELITE = 0;
constexpr uint8_t CROSSOVER = 1;
constexpr uint8_t MUTATE = 2;

using json = nlohmann::json;
using namespace std;
namespace fs = std::filesystem;

class geneticOptimizer
{
public:
    geneticOptimizer(json config)
    {
        setConfig(config);

        configureOptimizer();
    };

    auto getObjectiveFunction() -> function<double(shared_ptr<hist1D> a, shared_ptr<hist1D> b)>
    {
        auto funcType = config["objectiveFunc"].get<string>();

        switch (hashstr(funcType.c_str()))
        {
        [[deprecated("this is wrong, I think kl should be always positive")]] case hashstr("kl_divergence"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                double minClip = pow(10, -10);

                double kl = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {
                    double xVal = aRaw[i];
                    double yVal = bRaw[i];

                    if (xVal < minClip)
                    {
                        xVal = minClip;
                    }

                    if (yVal < minClip)
                    {
                        yVal = minClip;
                    }

                    kl += xVal * log(xVal / yVal);
                }

                return kl;
            };

            break;

        case hashstr("euclid"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                double euclid = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {
                    double xVal = aRaw[i];
                    double yVal = bRaw[i];

                    if ((xVal + yVal) == 0)
                    {
                        continue;
                    }

                    euclid += pow(xVal - yVal, 2);
                }

                return euclid;
            };

            break;

        case hashstr("manhattan"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                double dist = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {
                    double xVal = aRaw[i];
                    double yVal = bRaw[i];

                    if ((xVal + yVal) == 0)
                    {
                        continue;
                    }

                    dist += abs(xVal - yVal);
                }

                return dist;
            };

            break;

        case hashstr("bin_diff"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                double minClip = pow(10, -10);

                double kl = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {
                    double xVal = aRaw[i];
                    double yVal = bRaw[i];

                    if (xVal < minClip)
                    {
                        xVal = minClip;
                    }

                    if (yVal < minClip)
                    {
                        yVal = minClip;
                    }

                    kl += xVal / yVal;
                }

                return kl;
            };

            break;

        case hashstr("chi_squared"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                double chiSquare = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {
                    double xVal = aRaw[i];
                    double yVal = bRaw[i];

                    if ((xVal + yVal) == 0)
                    {
                        continue;
                    }

                    chiSquare += pow(xVal - yVal, 2) / (xVal + yVal);
                }

                return chiSquare;
            };

            break;

        case hashstr("pearson"):

            return [](shared_ptr<hist1D> a, shared_ptr<hist1D> b) -> double
            {
                assert(a->getNBins() == b->getNBins() && "histograms are not the same size");
                assert(a->getNBins() && "histogram has no size");

                auto aRaw = a->getNormData();
                auto bRaw = b->getNormData();

                auto aMean = accumulate(aRaw.begin(), aRaw.end(), 0.0) / aRaw.size();
                auto bMean = accumulate(bRaw.begin(), bRaw.end(), 0.0) / bRaw.size();

                double fTerm = 0.0;
                double sTerm = 0.0;
                double tTerm = 0.0;

                double aTerm = 0.0;
                double bTerm = 0.0;
                for (size_t i = 0; i < aRaw.size(); i++)
                {

                    aTerm = aRaw[i] - aMean;
                    bTerm = bRaw[i] - bMean;

                    fTerm += aTerm * bTerm;
                    sTerm += aTerm * aTerm;
                    tTerm += bTerm * bTerm;
                }

                double div = sqrt(sTerm * tTerm);

                if (div == 0)
                {
                    return 1;
                }

                double pearson = fTerm / sqrt(sTerm * tTerm);

                return 1 - pearson;
            };

            break;

        default:
            assert(false && "objective function not found");
            break;
        }

        return {};
    }

    auto configureOptimizer() -> void
    {
        objectiveFunction = getObjectiveFunction();
    }

    auto getSelectionVector() -> shared_ptr<vector<uint8_t>>
    {
        assert(config.contains("population") && "population not found in config");
        assert(config["population"].is_number_unsigned() && "population not found in config");

        auto selectionVect = make_shared<vector<uint8_t>>(config["population"].get<uint16_t>());

        for (uint16_t i = 0; i < selectionVect->size(); i++)
        {
            selectionVect->at(i) = stepDistr(rng);
        }

        return selectionVect;
    }

    auto runStep(uint32_t step) -> void
    {
        assert(crtPopulation->size() && "current population is empty");
        assert(crtFitness->size() && "current fitness is empty");

        auto selection = getSelectionVector();

        for (size_t idx = 0; idx < selection->size(); idx++)
        {
            unitStep(selection->at(idx), idx);
        }
    }

    auto eliteStep(uint16_t) -> void
    {

        auto minFit = min_element(crtFitness->begin(), crtFitness->end());

        uint16_t idx = distance(crtFitness->begin(), minFit);

        auto cPoint = crtPopulation->at(idx);

        shared_ptr<vector<vector<double>>> propVect = make_shared<vector<vector<double>>>(config["eliteSteps"].get<uint16_t>());

        for (uint16_t i = 0; i < config["eliteSteps"].get<uint16_t>(); i++)
        {
            vector<double> tempPar = vector<double>(cPoint.size());

            for (size_t o = 0; o < cPoint.size(); o++)
            {
                tempPar[o] = generateRandomNormal(cPoint[o]);
            }

            propVect->at(i) = tempPar;
        }

        auto propFitness = computeFitness(propVect);

        auto min = min_element(propFitness->begin(), propFitness->end());
        auto minIDx = distance(propFitness->begin(), min);

        if (*min < crtFitness->at(idx))
        {
            crtPopulation->at(idx) = propVect->at(minIDx);
            crtFitness->at(idx) = propFitness->at(minIDx);
        }
    }

    auto crossoverStep(uint16_t idx) -> void
    {
        auto rIdx = generateRandomInt16(0, crtPopulation->size() - 1);

        auto fPoint = crtPopulation->at(idx);
        auto sPoint = crtPopulation->at(rIdx);

        shared_ptr<vector<vector<double>>> mPoint = make_shared<vector<vector<double>>>(1);

        for (size_t i = 0; i < fPoint.size(); i++)
        {
            mPoint->at(0).push_back((fPoint[i] + sPoint[i]) / 2);
        }

        auto fit = computeFitness(mPoint);

        auto replaceIdx = idx;

        if (crtFitness->at(rIdx) > crtFitness->at(idx))
        {
            replaceIdx = rIdx;
        }

        crtPopulation->at(replaceIdx) = mPoint->at(0);
        crtFitness->at(replaceIdx) = fit->at(0);
    }

    auto mutateStep(uint16_t idx) -> void
    {
        auto randPos = generateRandomPopulation(1);

        auto randFit = computeFitness(randPos);

        crtPopulation->at(idx) = randPos->at(0);
        crtFitness->at(idx) = randFit->at(0);
    }

    auto unitStep(uint8_t sel, uint16_t idx) -> void
    {
        assert(sel < 3 && "selection is out of bounds");

        switch (sel)
        {
        case ELITE:
            eliteStep(idx);
            break;

        case CROSSOVER:
            crossoverStep(idx);
            break;

        case MUTATE:
            mutateStep(idx);
            break;

        default:
            break;
        }
    }

    auto checkState(uint32_t step) -> void
    {
        crtStep = step;

        auto min = min_element(crtFitness->begin(), crtFitness->end());
        auto minIdx = distance(crtFitness->begin(), min);

        auto mean = accumulate(crtFitness->begin(), crtFitness->end(), 0.0) / crtFitness->size();

        if (*min < bestFit)
        {
            bestFit = *min;
            bestPos = crtPopulation->at(minIdx);
        }
    }

    auto buildHist(shared_ptr<vector<double>> data, tuple<uint32_t, double, double> &histParam, vector<double> calib = {}) -> shared_ptr<hist1D>
    {

        auto hist = make_shared<hist1D>(get<0>(histParam), get<1>(histParam), get<2>(histParam));

        for (auto &d : *data)
        {
            auto val = calibrateValue(d, calib);

            hist->fill(val);
        }

        return hist;
    }

    auto setData(shared_ptr<vector<double>> rData, shared_ptr<vector<double>> iData) -> void
    {

        assert(rData && "reference data is null");
        assert(iData && "reference data is null");

        refHist = buildHist(rData, histParams);
        inpData = iData;
    }

    auto drawPopulation()
    {
    }

    auto optimize(shared_ptr<vector<double>> rData, shared_ptr<vector<double>> iData) -> void
    {

        assert(rData && "reference data is null");
        assert(iData && "reference data is null");

        setData(rData, iData);

        initializeOptimizer();

        assert(refHist && "reference hist is null");

        for (uint32_t i = 0; i < config["steps"].get<uint32_t>(); i++)
        {

            runStep(i);

            checkState(i);
        }
    }

    auto extractId(string a) -> int
    {
        auto path = fs::path{a};

        auto fName = path.filename().replace_extension("").string();

        auto pos = fName.find_last_of("_") + 1;

        auto id = fName.substr(pos);

        cout << "why here" << endl;

        return stoi(id);
    }

    auto generateRandomNormal(double mean, optional<double> sigma = {}) -> double
    {
        sigma = sigma.value_or(config["eliteSigma"]);

        auto normDistr = normal_distribution<double>(mean, sigma.value());

        return normDistr(rng);
    }

    auto generateRandomDouble(double min, double max) -> double
    {
        assert(min < max && "min is greater than max");

        std::uniform_real_distribution<double> distr(min, max);

        return distr(rng);
    }

    auto generateRandomInt16(int min, int max) -> uint16_t
    {
        assert(min < max && "min is greater than max");

        std::uniform_int_distribution<uint16_t> distr(min, max);

        return distr(rng);
    }

    auto generateRandomPopulation(optional<uint16_t> population = {}) -> shared_ptr<vector<vector<double>>>
    {
        population = population.value_or(config["population"].get<uint16_t>());

        shared_ptr<vector<vector<double>>> popVect = make_shared<vector<vector<double>>>(population.value());

        for (uint16_t i = 0; i < population.value(); i++)
        {
            vector<double> tempPar = vector<double>(getParametersSize());

            for (size_t o = 0; o < getParametersSize(); o++)
            {
                tempPar[o] = generateRandomDouble(parLimits[o][0], parLimits[o][1]);
            }

            popVect->at(i) = tempPar;
        }

        return popVect;
    }

    auto getRandomString(size_t len) -> string
    {
        static constexpr auto chars =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        auto dist = uniform_int_distribution{{}, strlen(chars) - 1};
        auto result = std::string(len, '\0');
        generate_n(begin(result), len, [&]()
                   { return chars[dist(rng)]; });

        return result;
    }

    auto calibrateValue(double nrg, vector<double> calib) -> double
    {
        nrg += generateRandomDouble(-0.5, 0.5);

        if (calib.size() == 0)
        {
            return nrg;
        }

        double calibValue = 0;

        for (uint i = 0; i < calib.size(); i++)
        {
            calibValue += calib[i] * pow(nrg, i);
        }

        return calibValue;
    }

    auto drawHistogram(shared_ptr<hist1D> hist1, shared_ptr<hist1D> hist2) -> void
    {
    }

    auto saveRoot(shared_ptr<hist1D> hist1, shared_ptr<hist1D> hist2, shared_ptr<hist1D> hist3) -> void
    {
    }

    auto drawHistogram(shared_ptr<hist1D> hist1) -> void
    {
    }

    auto computeFitness(optional<shared_ptr<vector<vector<double>>>> population = {}) -> shared_ptr<vector<double>>
    {

        population = population.value_or(crtPopulation);

        shared_ptr<vector<double>> fitnessVect = make_shared<vector<double>>(population.value()->size());

        for (size_t i = 0; i < fitnessVect->size(); i++)
        {

            auto inpHist = buildHist(inpData, histParams, population.value()->at(i));

            fitnessVect->at(i) = objectiveFunction(refHist, inpHist);
        }

        assert(fitnessVect->size() == population.value()->size() && "fitness vector size does not match population size");

        return fitnessVect;
    }

    auto initializeRandomGenerators(optional<uint32_t> seed) -> void
    {
        rng = std::mt19937(seed.value_or(dev()));

        assert(config.contains("selection") && "selection not found in config");
        assert(config["selection"].is_array() && "selection not array");
        assert(config["selection"].size() == 3 && "selection does not have size 3");

        auto selection = config["selection"].get<vector<double>>();

        stepDistr = discrete_distribution<>(selection.begin(), selection.end());
    }

    auto initializeOptimizer() -> void
    {
        initializeRandomGenerators({});

        if (config["steps"].get<uint16_t>())
        {

            crtPopulation = generateRandomPopulation();
            crtFitness = computeFitness();

            assert(crtPopulation->size() == config["population"].get<uint16_t>() && "population size does not match");

            // assert(objectiveFunction(refHist, refHist) == 0 && "objective function is not zero");
        }
    }

    auto getBestPos() -> vector<double>
    {
        return bestPos;
    }

    auto setParametersLimits() -> void
    {

        assert(config.contains("paraLimits") && "paraLimits not found in config");
        assert(config["paraLimits"].is_array() && "paraLimits is not array");

        for (auto &par : config["paraLimits"])
        {
            assert(par.is_array() && "paraLimits contains invalid data");
            assert(par.size() == 2 && "paraLimits contains invalid data");
        }

        parLimits = config["paraLimits"].get<vector<vector<double>>>();
    }

    auto getParametersSize() -> size_t
    {
        return parLimits.size();
    }

private:
    auto setConfig(json config) -> void
    {

        assert(config.is_null() == false && "config is null");
        this->config = config;

        setParametersLimits();

        assert(config.contains("population") && "population not found in config");
        assert(config["population"].is_number_unsigned() && "population not found in config");

        assert(config.contains("eliteSigma") && "eliteSigma not found in config");
        assert(config.contains("eliteSteps") && "eliteSteps not found in config");

        histParams = {config["histBins"], config["histRange"][0], config["histRange"][1]};
    }

    vector<vector<double>> parLimits;

    shared_ptr<hist1D> refHist;
    tuple<uint32_t, double, double> histParams;

    shared_ptr<vector<double>> inpData;

    shared_ptr<vector<vector<double>>> crtPopulation;
    shared_ptr<vector<double>> crtFitness;

    function<double(shared_ptr<hist1D> a, shared_ptr<hist1D> b)> objectiveFunction;

    json config;

    std::random_device dev;
    std::mt19937 rng;

    std::discrete_distribution<> stepDistr;

    vector<double> bestPos;
    double bestFit = numeric_limits<double>::max();
    int crtStep = 0;
};
