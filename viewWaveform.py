import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import savgol_filter
from scipy.ndimage import uniform_filter1d


def readLines(filename):

    waves = []

    try:
        with open(filename) as f:
            lines = f.readlines()

            for line in lines[4:]:

                line = line.rstrip().split(" ")

                waves.append([float(x) for x in line])

    except:
        pass

    return waves


def cfdTrigger(wave):

    frac = 0.6

    maxVal = np.max(wave)

    for idx in range(len(wave)):
        if wave[idx] > frac * maxVal:
            return idx


waves = readLines("garb")


# diffHist = TH1D("timeHist", "timeHist", 100, -100, 100)


for wave in waves:

    # trig = 150

    # cfd = cfdTrigger(wave)

    # sav_wave = savgol_filter(wave, 30, 2)

    # # cfdSavy = cfdTrigger(sav_wave)

    # grad = np.gradient(wave)
    # grad = uniform_filter1d(grad, size=50)

    # grad = grad / np.max(grad)
    # grad = grad * np.max(wave)

    # gradTrigger = cfdTrigger(grad)
    fix, ax = plt.subplots(1, 1)

    ax.plot(wave)
    # ax.plot(sav_wave)
    # ax.plot(grad)

    # ax.plot(grad)

    # xVals = [trig, trig]
    # yVals = [0, np.max(wave)]
    # ax.plot(xVals, yVals)

    # # xVals = [cfd, cfd]
    # # yVals = [0, np.max(wave)]
    # # ax.plot(xVals, yVals)

    # xVals = [gradTrigger, gradTrigger]
    # yVals = [0, np.max(wave)]
    # ax.plot(xVals, yVals)

    plt.show()

    # diffHist.Fill(trig - gradTrigger)


# diffHist.Draw("hist")
# gPad.WaitPrimitive("ggg")
