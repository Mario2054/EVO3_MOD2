#pragma once
#include <Arduino.h>

void eqAnalyzerInit();
void eqAnalyzerSetFromWeb(bool enabled);

void vuMeterMode5();
void vuMeterMode6();

void eqAnalyzerFeedSample(int16_t sampleL, int16_t sampleR);


