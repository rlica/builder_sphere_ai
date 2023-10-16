#pragma once

#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <TGraph.h>

using namespace std;

void removeBaseline(vector<int16_t> &wave, int16_t samples = 10);
uint64_t extractTime(vector<int16_t> &wave);
void draw_waveform(vector<int16_t> waveform);
void draw_waveform(vector<int16_t> waveform1, vector<int16_t> waveform2);
std::vector<float> moving_average(const std::vector<float> &in, unsigned int w);

vector<int16_t> derivative(vector<int16_t> &wave);

uint64_t extractTime(vector<int16_t> &wave)
{

    cout << "raw: ";

    for (int i = 0; i < 10; i++)
    {
        cout << wave[i] << " ";
    }
    cout << endl;

    removeBaseline(wave, 100);

    auto d = derivative(wave);

    draw_waveform(wave, d);

    cout << "base: ";
    for (int i = 0; i < 10; i++)
    {
        cout << wave[i] << " ";
    }
    cout << endl;

    return 0;
}

vector<int16_t> derivative(vector<int16_t> &wave)
{
    vector<int16_t> temp(wave.size());

    adjacent_difference(wave.begin(), wave.end(), temp.begin());

    return temp; // copy(temp.begin(), temp.end(), wave.begin());
}

void removeBaseline(vector<int16_t> &wave, int16_t samples)
{

    double baseline = accumulate(wave.begin(), wave.begin() + samples, 0.0);

    baseline /= samples;

    transform(wave.begin(), wave.end(), wave.begin(), [baseline](auto sample)
              { return sample - baseline; });
}

void draw_waveform(vector<int16_t> waveform)
{

    auto graph = TGraph();

    for (int i = 0; i < waveform.size(); i++)
    {
        graph.SetPoint(i + 1, i, waveform[i]);
    }

    double min = *max_element(waveform.begin(), waveform.end());
    double max = *min_element(waveform.begin(), waveform.end());

    if (min > max)
    {
        auto temp = min;
        min = max;
        max = temp;
    }

    if (min < 0)
    {
        min = min * 1.1;
    }
    else
    {
        min = min * 0.9;
    }

    max = max * 1.1;

    double dMax = *max_element(waveform.begin(), waveform.end());

    auto line1 = TGraph();

    double frac1 = 0.2 * dMax;

    for (int i = 0; i < waveform.size(); i++)
    {
        line1.AddPoint(i, frac1);
    }

    double frac2 = 0.4 * dMax;
    auto line2 = TGraph();

    for (int i = 0; i < waveform.size(); i++)
    {
        line2.AddPoint(i, frac2);
    }

    graph.GetYaxis()->SetRangeUser(min, max);

    graph.Draw();
    line1.Draw("same");
    line2.Draw("same");

    // gPad->WaitPrimitive("ggg");
    // gPad->Clear();
}

void draw_waveform(vector<int16_t> waveform1, vector<int16_t> waveform2)
{

    auto graph1 = TGraph();
    auto graph2 = TGraph();

    for (int i = 0; i < waveform1.size(); i++)
    {
        graph1.SetPoint(i + 1, i, waveform1[i]);
    }

    for (int i = 0; i < waveform2.size(); i++)
    {
        graph2.SetPoint(i + 1, i, waveform2[i]);
    }

    vector<double> limits;

    limits.push_back(*max_element(waveform1.begin(), waveform1.end()));
    limits.push_back(*min_element(waveform1.begin(), waveform1.end()));
    limits.push_back(*max_element(waveform2.begin(), waveform2.end()));
    limits.push_back(*min_element(waveform2.begin(), waveform2.end()));

    double min = *min_element(limits.begin(), limits.end());
    double max = *max_element(limits.begin(), limits.end());

    if (max > 0)
    {
        max = max * 1.1;
    }

    if (min < 0)
    {
        min = min * 1.1;
    }

    graph1.GetYaxis()->SetRangeUser(min, max);

    graph1.Draw();

    graph2.SetLineColor(2);
    graph2.Draw("same");

    gPad->WaitPrimitive("ggg");
    gPad->Clear();
}

std::vector<float> moving_average(const std::vector<float> &in, unsigned int w)
{
    using Index = std::make_signed_t<size_t>;

    if (w < 2)
    {
        return in;
    }

    // Make the window size odd
    w |= 1;
    // We use half the window at each end
    auto const h = w / 2;

    auto const len = static_cast<Index>(in.size());
    if (h >= len)
    {
        // window too big for data
        throw std::invalid_argument("w");
    }

    std::vector<float> out;
    out.reserve(in.size());

    auto const get_element = [len, w, &in](Index i)
    { return i < 0 ? in[-i] : i < len ? in[i]
                                      : in[2 * len - 1 - i]; };

    double rolling_sum = in[0] + 2 * std::accumulate(in.begin(), in.begin() + h, 0.0);
    for (Index i = 0; i < len; ++i)
    {
        out.push_back(static_cast<float>(rolling_sum / w));
        rolling_sum -= get_element(i - h);
        rolling_sum += get_element(i + h);
    }

    return out;
}