#pragma once
#include <Arduino.h>

// Ten nagłówek daje main.cpp wszystko czego potrzebuje do strony /analyzer:
// - AnalyzerStyleCfg + get/set/load/save
// - HTML generator + JSON
//
// Uwaga: Nie ma tu żadnego AudioRuntimeEQ_Evo.h.

struct AnalyzerStyleCfg {
  // ---- Styl 5 ----
  uint8_t s5_barWidth = 10;   // szerokość słupka (px)
  uint8_t s5_barGap   = 6;    // przerwa między słupkami (px)
  uint8_t s5_segments = 32;   // ilość segmentów w pionie
  float   s5_fill     = 0.60f;// wypełnienie segmentu (0.1..1.0) -> „klocek” jest niższy niż segmentStep

  // ---- Styl 6 ----
  uint8_t s6_gap    = 1;      // przerwa między kolumnami (px)
  uint8_t s6_shrink = 1;      // ile px odjąć z szerokości (żeby zostawić „ciemną kreskę”)
  float   s6_fill   = 0.60f;  // wypełnienie segmentu (0.1..1.0)
  uint8_t s6_segMin = 4;      // min segmentów
  uint8_t s6_segMax = 48;     // max segmentów (to mapujemy też na eq6_maxSegments)
};

AnalyzerStyleCfg analyzerGetStyle();
void analyzerSetStyle(const AnalyzerStyleCfg& c);

// Wczytaj/zapisz z STORAGE (SD/SPIFFS) – plik tekstowy /analyzer.cfg
void analyzerStyleLoad();
void analyzerStyleSave();

// Strona /analyzer i podgląd /analyzerCfg
String analyzerBuildHtmlPage();
String analyzerStyleToJson();

// Additional functions for analyzer control and display modes
void eqAnalyzerSetFromWeb(bool enabled);
void vuMeterMode5(); // Style 5: 16 bars with clock and speaker icon
void vuMeterMode6(); // Style 6: 16 thin bars with peak segments
