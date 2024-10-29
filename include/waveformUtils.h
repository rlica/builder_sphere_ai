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
