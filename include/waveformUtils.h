#pragma once

#include <iostream>
#include <vector>
#include "TROOT.h"
#include "TFile.h"
#include "TTree.h"
#include "TChain.h"
#include "TH1D.h"
#include "TPad.h"
#include <algorithm>
#include <iomanip>
#include <random>
#include "TGraph.h"

using namespace std;

inline vector<double> getWaveform(TArrayS *waveform);
inline void removeBaselineInplace(vector<double> &wave, int samples);
void dumpWaveform(vector<double> &wave);
inline vector<double>::iterator leadingEdge(vector<double> &wave, double threshold);
uint64_t calculateEletTime(vector<double> &wave, vector<double>::iterator first, vector<double>::iterator second, double threshold, double factor, uint64_t timestamp);
inline double interpolateTrigger(vector<double> &wave, vector<double>::iterator trigger, double threshold);
inline vector<double> getGradient(vector<double> &wave);

inline uint64_t getEletTime(TArrayS *waveform, ULong64_t timestamp)
{

    uint64_t time = timestamp;

    auto wave = getWaveform(waveform);

    removeBaselineInplace(wave, 50);

    // dumpWaveform(wave);

    double thres = 0;
    double fact = 2;

    auto first = leadingEdge(wave, thres);
    auto second = leadingEdge(wave, fact * thres);

    if (first != wave.end() && second != wave.end())
    {
        time = calculateEletTime(wave, first, second, thres, fact, timestamp);
    }

    return time;
}

inline uint64_t getEletTime(vector<double> &wave, ULong64_t timestamp)
{

    uint64_t time = timestamp;

    removeBaselineInplace(wave, 50);

    // dumpWaveform(wave);

    double thres = 200;
    double fact = 4;

    auto first = leadingEdge(wave, thres);
    auto second = leadingEdge(wave, fact * thres);

    if (first != wave.end() && second != wave.end())
    {
        time = calculateEletTime(wave, first, second, thres, fact, timestamp);
    }

    return time;
}

inline vector<double> uniformFilter(vector<double> &wave, int window = 3)
{
    vector<double> paddedInput = vector<double>(window / 2, wave.front());
    paddedInput.insert(paddedInput.end(), wave.begin(), wave.end());
    paddedInput.insert(paddedInput.end(), window / 2, wave.back());

    vector<double> filter;

    for (int i = window / 2; i < paddedInput.size() - window / 2; i++)
    {
        double sum = 0;
        for (int o = i - window / 2; o <= i + window / 2; o++)
        {
            sum += paddedInput[o];
        }

        filter.push_back(sum / window);
    }
    return filter;
}

template <typename T>
typename vector<T>::iterator leadingEdgeTrigger(const shared_ptr<vector<T>> &values, int16_t threshold, bool draw = false)
{

    function<bool(T)> checkFunc;

    if (threshold > 0)
    {
        checkFunc = [threshold](T element)
        { return element > threshold; };
    }
    else
    {
        checkFunc = [threshold](T element)
        { return element < threshold; };
    }

    auto it = find_if(values->begin(), values->end(), checkFunc);

    if (it == values->end())
    {
        return values->end();
    }

    return it;
}

template <typename T>
shared_ptr<vector<T>> subtractBackground(const shared_ptr<vector<T>> &values, int16_t noiseSamples)
{
    shared_ptr<vector<T>> result = make_shared<vector<T>>(values->size());

    double meanBackground = (accumulate(values->begin(), values->begin() + noiseSamples, static_cast<T>(0))) / noiseSamples;

    transform(values->begin(), values->end(),
              result->begin(), [meanBackground](T element)
              { return element - meanBackground; });

    return result;
}

template <typename T>
shared_ptr<vector<T>> reverseWaveform(const shared_ptr<vector<T>> &values)
{

    shared_ptr<vector<T>> reverse = make_shared<vector<T>>(values->size());

    transform(values->begin(), values->end(), reverse->begin(), [](auto element)
              { return -element; });

    return reverse;
}

template <typename T>
shared_ptr<vector<T>> attenuateWaveform(const shared_ptr<vector<T>> &values, double_t attenuation)
{
    shared_ptr<vector<T>> attenuated = make_shared<vector<T>>(values->size());

    transform(values->begin(), values->end(), attenuated->begin(), [attenuation](T element)
              { return static_cast<T>(attenuation * element); });
    return attenuated;
}

template <typename T>
shared_ptr<vector<T>> addWaveform(const shared_ptr<vector<T>> &wave1, const shared_ptr<vector<T>> &wave2)
{

    shared_ptr<vector<T>> add = make_shared<vector<T>>(wave1->size());

    transform(wave1->begin(), wave1->end(), wave2->begin(), add->begin(), [](auto wave1, auto wave2)
              { return wave1 + wave2; });

    return add;
}

template <typename T>
shared_ptr<vector<T>> smoothWaveform(const shared_ptr<vector<T>> &values, int window, bool draw)
{
    shared_ptr<vector<T>> smooth = make_shared<vector<T>>(values->size());

    for (int i = 0; i < values->size(); i++)
    {
        if (i < window || i >= values->size() - window)
        {
            (*smooth)[i] = (*values)[i];
            continue;
        }

        auto sum = accumulate(values->begin() + i, values->begin() + i + window, static_cast<T>(0.0));

        (*smooth)[i] = sum / window;
    }

    return smooth;
}

template <typename T>
shared_ptr<vector<T>> getBackgroundSamples(const shared_ptr<vector<T>> &wave, int16_t samples)
{
    double sum = std::accumulate(wave->begin(), wave->begin() + samples, 0.0);
    double mean = sum / samples;

    std::vector<T> diff(samples);

    std::transform(wave->begin(), wave->begin() + samples, diff.begin(), [mean](double x)
                   { return x - mean; });

    double sq_sum = std::inner_product(diff.begin(), diff.begin() + samples, diff.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / samples);

    shared_ptr<vector<T>> result = make_shared<vector<T>>(samples);

    std::default_random_engine generator;
    normal_distribution<T> distribution(mean, stdev);
    for (int i = 0; i < samples; i++)
    {
        result->at(i) = distribution(generator);
    }

    return result;
}

template <typename T>
shared_ptr<vector<T>> delayWaveform(const shared_ptr<vector<T>> &values, int16_t delay)
{
    auto bkgSamples = getBackgroundSamples(values, 50);

    bkgSamples->insert(bkgSamples->end(), values->begin(), values->end() - 50);

    return bkgSamples;
}

template <typename T>
shared_ptr<TGraph> getWaveformGraph(const shared_ptr<vector<T>> &values)
{
    shared_ptr<TGraph> graph(new TGraph(values->size()));

    for (int pointIndex = 0; pointIndex < values->size(); pointIndex++)
    {
        graph->SetPoint(pointIndex, pointIndex, (*values)[pointIndex]);
    }
    return graph;
}

template <typename T>
void drawWaveform(shared_ptr<vector<T>> &wave1)
{
    auto max_1 = *max_element(wave1->begin(), wave1->end());

    auto min_1 = *min_element(wave1->begin(), wave1->end());

    auto rightGraph = getWaveformGraph(wave1);

    rightGraph->GetYaxis()->SetRangeUser(min_1 - 200, max_1 + 200);

    rightGraph->SetTitle("accepted");

    rightGraph->Draw("APLM");
    gPad->Update();
    gPad->WaitPrimitive("ggg");
}

template <typename T>
void drawWaveform(shared_ptr<vector<T>> rightSub, shared_ptr<vector<T>> leftSub)
{

    auto max_1 = max_element(rightSub->begin(), rightSub->end());
    auto max_2 = max_element(leftSub->begin(), leftSub->end());

    auto min_1 = min_element(rightSub->begin(), rightSub->end());
    auto min_2 = min_element(leftSub->begin(), leftSub->end());

    auto max_val = max(*max_1, *max_2);
    auto min_val = min(*min_1, *min_2);

    auto rightGraph = getWaveformGraph(rightSub);
    auto leftGraph = getWaveformGraph(leftSub);

    rightGraph->GetYaxis()->SetRangeUser(min_val - 200, max_val + 200);

    leftGraph->SetLineColor(kRed);
    leftGraph->SetMarkerColor(kRed);

    rightGraph->SetTitle("accepted");

    rightGraph->Draw("APLM");
    leftGraph->Draw("same");
    gPad->Update();
    gPad->WaitPrimitive("ggg");
}

inline int32_t constantFractionDiscriminator(shared_ptr<vector<double>> values)
{

    int16_t threshold = -10;
    double_t attenuation = 0.5;
    int16_t delay = 12;
    int16_t smoothFactor = 8;
    int16_t tSample = 2;

    // drawWaveform(values);

    auto subBkg = subtractBackground(values, 50);

    auto inverted = reverseWaveform(subBkg);

    auto delayed = delayWaveform(subBkg, delay);

    auto attenuate = attenuateWaveform(inverted, attenuation);

    auto add = addWaveform(attenuate, delayed);

    auto smooth = smoothWaveform(add, smoothFactor, false);

    auto armIt = leadingEdgeTrigger(smooth, threshold);

    if (armIt == smooth->end())
    {
        return 0;
    }

    auto afterZero = find_if(armIt, smooth->end(), [](auto element)
                             { return element > 0; });

    auto beforeZero = afterZero - 1;

    uint16_t tCoarse = distance(smooth->begin(), afterZero) * tSample * 1000; // to picoseconds

    return tCoarse;
}

inline uint64_t getCFDTime(vector<double> &wave, ULong64_t timestamp)
{

    uint64_t time = timestamp;

    removeBaselineInplace(wave, 50);

    auto grad = getGradient(wave);

    auto filtered = uniformFilter(grad, 50);

    // dumpWaveform(grad);

    double maxValue = *max_element(filtered.begin(), filtered.end());
    double frac = 0.4;

    double thres = frac * maxValue;

    auto first = leadingEdge(filtered, thres);

    auto cfdTrigger = distance(filtered.begin(), first);

    auto diff = cfdTrigger - 150;

    return timestamp + diff * 2000;
}

inline uint64_t getWaveFraction(vector<double> &wave, ULong64_t timestamp)
{

    uint64_t time = timestamp;

    removeBaselineInplace(wave, 50);

    auto filtered = uniformFilter(wave, 50);

    // dumpWaveform(grad);

    double maxValue = *max_element(filtered.begin(), filtered.end());
    double frac = 0.4;

    double thres = frac * maxValue;

    auto first = leadingEdge(filtered, thres);

    auto cfdTrigger = distance(filtered.begin(), first);

    auto diff = cfdTrigger - 150;

    return timestamp + diff * 2000;
}

inline vector<double> getGradient(vector<double> &wave)
{
    vector<double> grad = vector<double>(wave.size(), 0);
    double value;

    for (int i = 0; i < wave.size(); i++)
    {
        if (i == 0)
        {
            value = wave[i + 1] - wave[i];
        }
        else if (i == wave.size() - 1)
        {
            value = wave[i] - wave[i - 1];
        }
        else
        {
            value = (wave[i + 1] - wave[i - 1]) / 2;
        }

        grad[i] = value;
    }

    return grad;
}

inline vector<double> getWaveform(TArrayS *waveform)
{
    vector<double> wave = vector<double>(waveform->GetSize());

    for (int i = 0; i < waveform->GetSize(); i++)
    {
        wave[i] = waveform->At(i);
    }

    return wave;
}

inline void removeBaselineInplace(vector<double> &wave, int samples)
{
    auto baseline = accumulate(wave.begin(), wave.begin() + samples, 0.0);

    baseline /= samples;

    transform(wave.begin(), wave.end(), wave.begin(), [baseline](auto x)
              { return x - baseline; });
}

inline void dumpWaveform(vector<double> &wave)
{
    for (int i = 0; i < wave.size(); i++)
    {
        cout << wave[i] << " ";
    }
    cout << endl;
}

inline vector<double>::iterator leadingEdge(vector<double> &wave, double threshold)
{

    function<bool(double)> func;

    if (threshold > 0)
    {
        func = function<bool(double)>([threshold](auto x)
                                      { return x > threshold; });
    }
    else
    {
        func = function<bool(double)>([threshold](auto x)
                                      { return x < threshold; });
    }

    return find_if(wave.begin(), wave.end(), func);
}

inline double interpolateTrigger(vector<double> &wave, vector<double>::iterator trigger, double threshold)
{
    auto idx = distance(wave.begin(), trigger);

    double calcIdx = idx;
    double diff = wave[idx] - wave[idx - 1];

    if (diff)
    {
        calcIdx = idx - 1 + (threshold - wave[idx - 1]) / diff;
    }

    return calcIdx;
}

inline uint64_t calculateEletTime(vector<double> &wave, vector<double>::iterator first, vector<double>::iterator second, double threshold, double factor, uint64_t timestamp)
{

    auto firstPass = interpolateTrigger(wave, first, threshold);
    auto secondPass = interpolateTrigger(wave, second, factor * threshold);

    double extrapolate = 1.1;
    double time = ((firstPass * (factor + extrapolate) - secondPass * (1 + extrapolate)) / (factor - 1)) + timestamp;

    return time;
}
