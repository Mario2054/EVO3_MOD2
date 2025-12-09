#include "EQ_FFTAnalyzer.h"
#include "Audio.h"
#include <U8g2lib.h>
#include <time.h>
#include <arduinoFFT.h>

static const uint16_t FFT_SIZE = 256;
static const uint8_t  NUM_BANDS = 16;

extern U8G2 u8g2;
extern Audio audio;

extern bool volumeMute;
extern uint8_t volumeValue;

extern String stationName;
extern String stationNameStream;
extern String stationStringWeb;

extern bool eqAnalyzerOn;

static double vReal[FFT_SIZE];
static double vImag[FFT_SIZE];
static uint16_t sampleIndex = 0;

static float bandLevel[NUM_BANDS];
static float bandPeak[NUM_BANDS];
static uint8_t bandPeakHold[NUM_BANDS];

static const float bandWeight[NUM_BANDS] = {
    1.40f, 1.28f, 1.18f, 1.10f,
    1.02f, 0.96f, 0.90f, 0.85f,
    0.80f, 0.76f, 0.72f, 0.68f,
    0.64f, 0.60f, 0.56f, 0.52f
};

static ArduinoFFT<double> FFT(vReal, vImag, FFT_SIZE, 44100.0);

void eqAnalyzerInit()
{
    sampleIndex = 0;
    for (uint8_t i = 0; i < NUM_BANDS; ++i)
    {
        bandLevel[i] = 0.0f;
        bandPeak[i] = 0.0f;
        bandPeakHold[i] = 0;
    }
}

void eqAnalyzerSetFromWeb(bool enabled)
{
    eqAnalyzerOn = enabled;
    if (!enabled)
    {
        for (uint8_t i = 0; i < NUM_BANDS; ++i)
        {
            bandLevel[i] = 0.0f;
            bandPeak[i] = 0.0f;
            bandPeakHold[i] = 0;
        }
        sampleIndex = 0;
    }
}

void eqAnalyzerFeedSample(int16_t sampleL, int16_t sampleR)
{
    if (!eqAnalyzerOn) return;

    int32_t mix = (int32_t)sampleL + (int32_t)sampleR;
    mix /= 2;

    vReal[sampleIndex] = (double)mix / 32768.0;
    vImag[sampleIndex] = 0.0;

    sampleIndex++;

    if (sampleIndex >= FFT_SIZE)
    {
        sampleIndex = 0;

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        uint16_t binStart = 2;
        uint16_t binEnd   = FFT_SIZE / 2;

        float lastEdge = log10f((float)binStart);
        float maxEdge  = log10f((float)binEnd);

        for (uint8_t b = 0; b < NUM_BANDS; ++b)
        {
            float edge   = lastEdge + (maxEdge - lastEdge) / (float)(NUM_BANDS - b);
            float fStart = powf(10.0f, lastEdge);
            float fEnd   = powf(10.0f, edge);

            uint16_t iStart = (uint16_t)fStart;
            uint16_t iEnd   = (uint16_t)fEnd;
            if (iStart < binStart) iStart = binStart;
            if (iEnd   > binEnd)   iEnd   = binEnd;
            if (iEnd <= iStart)    iEnd   = iStart + 1;

            double sum = 0.0;
            for (uint16_t i = iStart; i < iEnd; ++i)
                sum += vReal[i];
            double avg = sum / (double)(iEnd - iStart);
            if (avg < 0.0) avg = 0.0;

            double lvl = log10(1.0 + avg * 20.0);
            if (lvl < 0.0) lvl = 0.0;
            if (lvl > 1.0) lvl = 1.0;

            float fLevel = (float)lvl * bandWeight[b];
            if (fLevel > 1.0f) fLevel = 1.0f;

            bandLevel[b] = fLevel;

            if (fLevel > bandPeak[b])
            {
                bandPeak[b] = fLevel;
                bandPeakHold[b] = 6;
            }
            else
            {
                if (bandPeakHold[b] > 0)
                    bandPeakHold[b]--;
                else
                {
                    bandPeak[b] -= 0.04f;
                    if (bandPeak[b] < 0.0f) bandPeak[b] = 0.0f;
                }
            }

            lastEdge = edge;
        }
    }
}

static void drawHeaderBar()
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5))
    {
        char timeString[9];
        if (timeinfo.tm_sec % 2 == 0)
            snprintf(timeString, sizeof(timeString), "%2d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        else
            snprintf(timeString, sizeof(timeString), "%2d %02d", timeinfo.tm_hour, timeinfo.tm_min);

        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.setCursor(4, 11);
        u8g2.print(timeString);

        uint8_t timeWidth = u8g2.getStrWidth(timeString);
        uint8_t xStation  = 4 + timeWidth + 6;

        uint8_t iconX = 256 - 40;
        uint8_t maxStationWidth = 0;
        if (iconX > xStation + 4)
            maxStationWidth = iconX - xStation - 4;

        if (maxStationWidth > 0)
        {
            String nameToShow = stationName;
            if (nameToShow.length() == 0)
            {
                if (stationNameStream.length() > 0)      nameToShow = stationNameStream;
                else if (stationStringWeb.length() > 0)  nameToShow = stationStringWeb;
                else                                     nameToShow = "Radio";
            }

            while (nameToShow.length() > 0 &&
                   u8g2.getStrWidth(nameToShow.c_str()) > maxStationWidth)
            {
                nameToShow.remove(nameToShow.length() - 1);
            }

            u8g2.setCursor(xStation, 11);
            u8g2.print(nameToShow);
        }
    }

    uint8_t iconY = 2;
    uint8_t iconX = 256 - 40;

    u8g2.drawBox(iconX, iconY + 2, 4, 7);
    u8g2.drawLine(iconX + 4, iconY + 2, iconX + 7, iconY);
    u8g2.drawLine(iconX + 4, iconY + 8, iconX + 7, iconY + 10);
    u8g2.drawLine(iconX + 7, iconY,     iconX + 7, iconY + 10);

    u8g2.drawPixel(iconX + 9,  iconY + 3);
    u8g2.drawPixel(iconX + 10, iconY + 5);
    u8g2.drawPixel(iconX + 9,  iconY + 7);

    u8g2.setFont(u8g2_font_5x8_mr);
    u8g2.setCursor(iconX + 14, 10);
    u8g2.print(volumeValue);

    u8g2.drawHLine(0, 13, 256);
}

static void drawStyleCommon(bool thinLines)
{
    u8g2.setDrawColor(1);
    u8g2.clearBuffer();

    if (!eqAnalyzerOn)
    {
        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.setCursor(10, 24);
        u8g2.print("ANALYZER OFF");
        u8g2.setCursor(10, 40);
        u8g2.print("Enable in Web UI");
        u8g2.sendBuffer();
        return;
    }

    drawHeaderBar();

    const uint8_t eqTopY      = 14;
    const uint8_t eqBottomY   = 63;
    const uint8_t eqMaxHeight = eqBottomY - eqTopY + 1;

    const uint8_t maxSegments = thinLines ? 28 : 32;
    const float   segmentStep = (float)eqMaxHeight / (float)maxSegments;

    const uint8_t barWidth = thinLines ? 6 : 10;
    const uint8_t barGap   = thinLines ? 4 : 6;

    const uint16_t totalBarsWidth = NUM_BANDS * barWidth + (NUM_BANDS - 1) * barGap;
    int16_t startX = (256 - totalBarsWidth) / 2;
    if (startX < 0) startX = 0;

    for (uint8_t i = 0; i < NUM_BANDS; ++i)
    {
        float level = bandLevel[i];
        float peak  = bandPeak[i];

        if (level < 0.0f) level = 0.0f;
        if (level > 1.0f) level = 1.0f;
        if (peak  < 0.0f) peak  = 0.0f;
        if (peak  > 1.0f) peak  = 1.0f;

        uint8_t segCount = (uint8_t)(level * maxSegments + 0.5f);
        uint8_t peakSeg  = (uint8_t)(peak  * maxSegments + 0.5f);

        uint8_t x = startX + i * (barWidth + barGap);

        for (uint8_t s = 0; s < segCount; ++s)
        {
            int16_t segBottom = eqBottomY - (int16_t)(s * segmentStep);
            int16_t segTop    = thinLines ? segBottom - 1
                                          : segBottom - (int16_t)(segmentStep * 0.6f);

            if (segTop < eqTopY) segTop = eqTopY;
            if (segBottom < eqTopY) break;

            if (thinLines)
            {
                if (segBottom < segTop) segBottom = segTop;
                u8g2.drawHLine(x, segBottom, barWidth);
            }
            else
            {
                uint8_t segH = (uint8_t)(segBottom - segTop + 1);
                if (segH < 1) segH = 1;
                u8g2.drawBox(x, segTop, barWidth, segH);
            }
        }

        if (peakSeg > 0)
        {
            uint8_t ps = peakSeg - 1;
            int16_t peakBottom = eqBottomY - (int16_t)(ps * segmentStep);
            int16_t peakY      = peakBottom - 1;
            if (peakY >= eqTopY && peakY <= eqBottomY)
            {
                if (thinLines)
                    u8g2.drawHLine(x, peakY, barWidth);
                else
                    u8g2.drawBox(x, peakY, barWidth, 1);
            }
        }
    }

    if (volumeMute)
    {
        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.setDrawColor(0);
        u8g2.drawBox(60, (64/2) - 10, 256-120, 20);
        u8g2.setDrawColor(1);
        u8g2.setCursor((64/2) - 30, (64/2) + 4);
        u8g2.print("MUTED");
    }

    u8g2.sendBuffer();
}

void vuMeterMode5()
{
    drawStyleCommon(false);
}

void vuMeterMode6()
{
    drawStyleCommon(true);
}

