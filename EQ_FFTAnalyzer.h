#pragma once
#include <Arduino.h>

// Analizator pasmowy bez ArduinoFFT (Goertzel) – zero dodatkowych bibliotek.
// Działa tylko jeśli biblioteka audio (ESP32-audioI2S) wywołuje audio_process_i2s().

#ifndef RUNTIME_EQ_BANDS
#define RUNTIME_EQ_BANDS 16
#endif

// Sterowanie z WWW / main
extern bool eqAnalyzerEnabled;     // włącz/wyłącz analizator (style 5/6)
extern uint8_t eq6_maxSegments;    // ile segmentów używa styl 6 (mapowane z WWW)
extern uint8_t eq5_maxSegments;    // ile segmentów używa styl 5  
extern uint8_t eq_barWidth5, eq_barGap5;  // szerokość i gap dla trybu 5
extern uint8_t eq_barWidth6, eq_barGap6;  // szerokość i gap dla trybu 6

void eq_analyzer_init(void);
void eq_analyzer_reset(void);
void eq_analyzer_set_enabled(bool enabled);

// 0..1 dla każdego pasma
void eq_get_analyzer_levels(float* outLevels); // outLevels[RUNTIME_EQ_BANDS]
void eq_get_analyzer_peaks(float* outPeaks);   // outPeaks[RUNTIME_EQ_BANDS]

// Hook z biblioteki ESP32-audioI2S (musi zostać wywołany przez Audio.cpp)
void audio_process_i2s(int16_t* outBuff, int32_t validSamples, bool* continueI2S);

// Funkcje diagnostyczne
bool eq_analyzer_is_receiving_samples();
uint32_t eq_analyzer_get_sample_count();
void eq_analyzer_enable_test_generator(bool enable);

// Funkcje konfiguracji analizatora
void eq_set_style5_params(uint8_t segments, uint8_t barWidth, uint8_t barGap);
void eq_set_style6_params(uint8_t segments, uint8_t barWidth, uint8_t barGap);
void eq_auto_fit_width(uint8_t style, uint16_t screenWidth);
