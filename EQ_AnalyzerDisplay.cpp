#include "EQ_AnalyzerDisplay.h"
#include "EQ_FFTAnalyzer.h"

#include <FS.h>
#include <U8g2lib.h>
#include <time.h>

// Function to get storage from main.cpp
extern fs::FS& getStorage();
extern String stationName;
extern String stationNameStream;
extern String stationStringWeb;
extern uint8_t volumeValue;
extern bool volumeMute;
extern U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2;

// EQ variables - defined locally since they are simple arrays
const uint8_t EQ_BANDS = 16;
extern uint8_t eqLevel[16];
extern uint8_t eqPeak[16];

static const char* kCfgPath = "/analyzer.cfg";
static AnalyzerStyleCfg g_cfg;

static uint8_t clampU8(int v, int lo, int hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return (uint8_t)v;
}
static float clampF(float v, float lo, float hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}

AnalyzerStyleCfg analyzerGetStyle() { return g_cfg; }

void analyzerSetStyle(const AnalyzerStyleCfg& in)
{
  AnalyzerStyleCfg c = in;

  // Styl 5
  c.s5_barWidth = clampU8(c.s5_barWidth, 2, 30);
  c.s5_barGap   = clampU8(c.s5_barGap,   0, 20);
  c.s5_segments = clampU8(c.s5_segments,  4, 48);
  c.s5_fill     = clampF(c.s5_fill,     0.10f, 1.00f);

  // Styl 6
  c.s6_gap    = clampU8(c.s6_gap,    0, 10);
  c.s6_shrink = clampU8(c.s6_shrink, 0, 5);
  c.s6_fill   = clampF(c.s6_fill,   0.10f, 1.00f);
  c.s6_segMin = clampU8(c.s6_segMin, 4, 48);
  c.s6_segMax = clampU8(c.s6_segMax, 4, 48);
  if (c.s6_segMax < c.s6_segMin) c.s6_segMax = c.s6_segMin;

  g_cfg = c;

  // Mapuj konfigurację na nasze zmienne globalne
  eq5_maxSegments = g_cfg.s5_segments;
  eq_barWidth5 = g_cfg.s5_barWidth;
  eq_barGap5 = g_cfg.s5_barGap;
  
  eq6_maxSegments = g_cfg.s6_segMax;
  eq_barWidth6 = 10;  // będzie obliczone automatycznie w eq_auto_fit_width
  eq_barGap6 = g_cfg.s6_gap;
}

static bool parseLineKV(const String& line, String& k, String& v) {
  int eq = line.indexOf('=');
  if (eq <= 0) return false;
  k = line.substring(0, eq); k.trim();
  v = line.substring(eq+1);  v.trim();
  return k.length() > 0;
}

void analyzerStyleLoad()
{
  // default
  g_cfg = AnalyzerStyleCfg();
  eq6_maxSegments = g_cfg.s6_segMax;

  File f = getStorage().open(kCfgPath, FILE_READ);
  if (!f) return;

  AnalyzerStyleCfg c = g_cfg;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line.startsWith("#")) continue;

    String k, v;
    if (!parseLineKV(line, k, v)) continue;

    if (k == "s5w")    c.s5_barWidth = (uint8_t)v.toInt();
    else if (k == "s5g")    c.s5_barGap   = (uint8_t)v.toInt();
    else if (k == "s5seg")  c.s5_segments = (uint8_t)v.toInt();
    else if (k == "s5fill") c.s5_fill     = v.toFloat();

    else if (k == "s6g")    c.s6_gap      = (uint8_t)v.toInt();
    else if (k == "s6sh")   c.s6_shrink   = (uint8_t)v.toInt();
    else if (k == "s6fill") c.s6_fill     = v.toFloat();
    else if (k == "s6min")  c.s6_segMin   = (uint8_t)v.toInt();
    else if (k == "s6max")  c.s6_segMax   = (uint8_t)v.toInt();
  }
  f.close();

  analyzerSetStyle(c);
}

void analyzerStyleSave()
{
  File f = getStorage().open(kCfgPath, FILE_WRITE);
  if (!f) return;

  f.println("# Analyzer style cfg");
  f.println("# Style5");
  f.printf("s5w=%u\n", g_cfg.s5_barWidth);
  f.printf("s5g=%u\n", g_cfg.s5_barGap);
  f.printf("s5seg=%u\n", g_cfg.s5_segments);
  f.printf("s5fill=%.3f\n", g_cfg.s5_fill);

  f.println("# Style6");
  f.printf("s6g=%u\n", g_cfg.s6_gap);
  f.printf("s6sh=%u\n", g_cfg.s6_shrink);
  f.printf("s6fill=%.3f\n", g_cfg.s6_fill);
  f.printf("s6min=%u\n", g_cfg.s6_segMin);
  f.printf("s6max=%u\n", g_cfg.s6_segMax);
  f.close();
}

String analyzerStyleToJson()
{
  String s;
  s.reserve(220);
  s += "{";
  s += "\"s5_barWidth\":" + String(g_cfg.s5_barWidth) + ",";
  s += "\"s5_barGap\":"   + String(g_cfg.s5_barGap) + ",";
  s += "\"s5_segments\":" + String(g_cfg.s5_segments) + ",";
  s += "\"s5_fill\":"     + String(g_cfg.s5_fill, 3) + ",";

  s += "\"s6_gap\":"      + String(g_cfg.s6_gap) + ",";
  s += "\"s6_shrink\":"   + String(g_cfg.s6_shrink) + ",";
  s += "\"s6_fill\":"     + String(g_cfg.s6_fill, 3) + ",";
  s += "\"s6_segMin\":"   + String(g_cfg.s6_segMin) + ",";
  s += "\"s6_segMax\":"   + String(g_cfg.s6_segMax);
  s += "}";
  return s;
}

static String htmlHeader(const char* title)
{
  String s;
  s.reserve(6000);
  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>"; s += title; s += "</title>";
  s += "<style>";
  s += "body{font-family:Arial;margin:14px;max-width:900px}";
  s += "h2{margin:8px 0 12px 0}";
  s += ".box{border:1px solid #ddd;border-radius:10px;padding:12px;margin:10px 0}";
  s += ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:8px 0}";
  s += "label{min-width:120px} input{width:90px;padding:6px}";
  s += "button{padding:10px 12px;border:0;border-radius:10px;background:#222;color:#fff;cursor:pointer}";
  s += "button.secondary{background:#555} a{color:#0a58ca}";
  s += "</style></head><body>";
  return s;
}

String analyzerBuildHtmlPage()
{
  String s = htmlHeader("Analyzer / Style 5-6");
  s += "<h2>Analizator – ustawienia stylów 5 i 6</h2>";
  s += "<p>Tu regulujesz WYGLĄD słupków. Same słupki ruszą dopiero, gdy w /config zaznaczysz <b>FFT analyzer</b>.</p>";

  s += "<form method='POST'>";

  s += "<div class='box'><h3>Styl 5</h3>";
  s += "<div class='row'><label>bar width</label><input name='s5w' type='number' min='2' max='30' value='" + String(g_cfg.s5_barWidth) + "'></div>";
  s += "<div class='row'><label>bar gap</label><input name='s5g' type='number' min='0' max='20' value='" + String(g_cfg.s5_barGap) + "'></div>";
  s += "<div class='row'><label>segments</label><input name='s5seg' type='number' min='4' max='48' value='" + String(g_cfg.s5_segments) + "'></div>";
  s += "<div class='row'><label>fill (0.1..1)</label><input step='0.05' name='s5fill' type='number' min='0.1' max='1.0' value='" + String(g_cfg.s5_fill,2) + "'></div>";
  s += "</div>";

  s += "<div class='box'><h3>Styl 6</h3>";
  s += "<div class='row'><label>gap</label><input name='s6g' type='number' min='0' max='10' value='" + String(g_cfg.s6_gap) + "'></div>";
  s += "<div class='row'><label>shrink</label><input name='s6sh' type='number' min='0' max='5' value='" + String(g_cfg.s6_shrink) + "'></div>";
  s += "<div class='row'><label>fill (0.1..1)</label><input step='0.05' name='s6fill' type='number' min='0.1' max='1.0' value='" + String(g_cfg.s6_fill,2) + "'></div>";
  s += "<div class='row'><label>seg min</label><input name='s6min' type='number' min='4' max='48' value='" + String(g_cfg.s6_segMin) + "'></div>";
  s += "<div class='row'><label>seg max</label><input name='s6max' type='number' min='4' max='48' value='" + String(g_cfg.s6_segMax) + "'></div>";
  s += "</div>";

  s += "<div class='row'>";
  s += "<button formaction='/analyzerApply' type='submit'>Podgląd LIVE</button>";
  s += "<button class='secondary' formaction='/analyzerSave' type='submit'>Zapisz</button>";
  s += "<a href='/analyzerCfg' style='margin-left:12px'>JSON</a>";
  s += "</div>";

  s += "</form></body></html>";
  return s;
}

// ─────────────────────────────────────
// Additional analyzer functions
// ─────────────────────────────────────

void eqAnalyzerSetFromWeb(bool enabled)
{
  eqAnalyzerEnabled = enabled;
}

// ─────────────────────────────────────
// STYL 5 – 16 słupków, zegar + ikonka głośnika
// ─────────────────────────────────────

void vuMeterMode5() // Tryb 5: 16 słupków – dynamiczny analizator z zegarem i ikonką głośnika
{
  // Jeśli analizator jest wyłączony – pokaż prosty komunikat
  if (!eqAnalyzerEnabled)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(10, 24);
    u8g2.print("ANALYZER OFF");
    u8g2.setCursor(10, 40);
    u8g2.print("Enable in Web UI");
    u8g2.sendBuffer();
    return;
  }

  // Sprawdź czy próbki napływają
  static uint32_t noSamplesTime5 = 0;
  if (!eq_analyzer_is_receiving_samples())
  {
    if (noSamplesTime5 == 0) noSamplesTime5 = millis();
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(10, 16);
    u8g2.print("NO AUDIO SAMPLES");
    u8g2.setCursor(10, 32);
    u8g2.print("Count: ");
    u8g2.print(eq_analyzer_get_sample_count());
    
    // Po 3 sekundach włącz generator testowy
    if (millis() - noSamplesTime5 > 3000) {
      u8g2.setCursor(10, 48);
      u8g2.print("Enabling test mode...");
      eq_analyzer_enable_test_generator(true);
      noSamplesTime5 = millis(); // Reset timer
    } else {
      u8g2.setCursor(10, 48);
      u8g2.print("Check audio source");
    }
    u8g2.sendBuffer();
    return;
  } else {
    noSamplesTime5 = 0; // Reset when receiving samples
    eq_analyzer_enable_test_generator(false); // Wyłącz generator gdy mamy audio
  }

  // 1. Pobranie poziomów z analizatora FFT (0..1)
  float levels[RUNTIME_EQ_BANDS];
  float peaks[RUNTIME_EQ_BANDS];
  eq_get_analyzer_levels(levels);
  eq_get_analyzer_peaks(peaks);

  // Debug co 2 sekundy - sprawdź wartości poziomów
  static uint32_t lastDebugDisplay = 0;
  uint32_t now = millis();
  if (now - lastDebugDisplay > 2000) {
    Serial.print("Display levels: ");
    for (int i = 0; i < min((int)EQ_BANDS, (int)RUNTIME_EQ_BANDS); i++) {
      Serial.print(levels[i], 2);
      if (i < min((int)EQ_BANDS, (int)RUNTIME_EQ_BANDS) - 1) Serial.print(" ");
    }
    Serial.println();
    lastDebugDisplay = now;
  }

  // Przepisujemy do eqLevel/eqPeak w skali 0..100 dla rysowania słupków
  for (uint8_t i = 0; i < EQ_BANDS && i < RUNTIME_EQ_BANDS; i++)
  {
    float lv = levels[i];
    float pk = peaks[i];
    if (lv < 0.0f) lv = 0.0f;
    if (lv > 1.0f) lv = 1.0f;
    if (pk < 0.0f) pk = 0.0f;
    if (pk > 1.0f) pk = 1.0f;

    eqLevel[i] = (uint8_t)(lv * 100.0f + 0.5f);
    eqPeak[i]  = (uint8_t)(pk * 100.0f + 0.5f);
  }

  // 2. Rysowanie – zegar + ikonka głośnika u góry, słupki pod spodem
  u8g2.setDrawColor(1);
  u8g2.clearBuffer();

  // Pasek górny: zegar po lewej, stacja obok, ikonka głośnika po prawej
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

    // Nazwa stacji obok zegara
    uint8_t timeWidth = u8g2.getStrWidth(timeString);
    uint8_t xStation  = 4 + timeWidth + 6;

    // Zarezerwuj miejsce do ikony głośnika po prawej
    uint8_t iconX = 256 - 40;  // SCREEN_WIDTH = 256
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

      // Przycinanie tekstu do wolnej szerokości
      while (nameToShow.length() > 0 &&
             u8g2.getStrWidth(nameToShow.c_str()) > maxStationWidth)
      {
        nameToShow.remove(nameToShow.length() - 1);
      }

      u8g2.setCursor(xStation, 11);
      u8g2.print(nameToShow);
    }
  }

  // Ikonka głośnika + wartość głośności po prawej
  uint8_t iconY = 2;
  uint8_t iconX = 256 - 40;  // SCREEN_WIDTH = 256

  // „kolumna" głośnika
  u8g2.drawBox(iconX, iconY + 2, 4, 7);
  // przód głośnika – linie
  u8g2.drawLine(iconX + 4, iconY + 2, iconX + 7, iconY);      // skośna góra
  u8g2.drawLine(iconX + 4, iconY + 8, iconX + 7, iconY + 10); // skośny dół
  u8g2.drawLine(iconX + 7, iconY,     iconX + 7, iconY + 10); // pion

  // „fale" dźwięku
  u8g2.drawPixel(iconX + 9,  iconY + 3);
  u8g2.drawPixel(iconX + 10, iconY + 5);
  u8g2.drawPixel(iconX + 9,  iconY + 7);

  // Wartość głośności
  u8g2.setFont(u8g2_font_5x8_mr);
  u8g2.setCursor(iconX + 14, 10);
  u8g2.print(volumeValue);

  // Linia oddzielająca pasek od słupków
  u8g2.drawHLine(0, 13, 256);  // SCREEN_WIDTH = 256

  // Obszar słupków – od linii w dół do końca ekranu
  const uint8_t eqTopY      = 14;                    // pod paskiem
  const uint8_t eqBottomY   = 64 - 1;               // SCREEN_HEIGHT = 64, do dołu
  const uint8_t eqMaxHeight = eqBottomY - eqTopY + 1;

  // Konfigurowalna liczba segmentów
  const uint8_t maxSegments = eq5_maxSegments;
  const float segmentStep = (float)eqMaxHeight / (float)maxSegments;

  // Auto-dopasowanie do pełnej szerokości ekranu
  eq_auto_fit_width(5, 256);
  
  // Parametry słupków – 16 sztuk (konfigurowalne)
  const uint8_t barWidth = eq_barWidth5;
  const uint8_t barGap   = eq_barGap5;

  const uint16_t totalBarsWidth = EQ_BANDS * barWidth + (EQ_BANDS - 1) * barGap;
  int16_t startX = (256 - totalBarsWidth) / 2;  // SCREEN_WIDTH = 256
  if (startX < 2) startX = 2;  // Minimalny margines

  // Rysowanie słupków z peakami
  for (uint8_t i = 0; i < EQ_BANDS; i++)
  {
    uint8_t levelPercent = eqLevel[i];  // 0-100
    uint8_t peakPercent  = eqPeak[i];   // 0-100

    // Liczba segmentów w słupku
    uint8_t segments = (levelPercent * maxSegments) / 100;
    if (segments > maxSegments) segments = maxSegments;

    // Pozycja „peak" w segmentach
    uint8_t peakSeg = (peakPercent * maxSegments) / 100;
    if (peakSeg > maxSegments) peakSeg = maxSegments;

    // x słupka
    int16_t x = startX + i * (barWidth + barGap);

    // Rysujemy segmenty od dołu
    for (uint8_t s = 0; s < segments; s++)
    {
      int16_t segBottom = eqBottomY - (int16_t)(s * segmentStep);
      int16_t segTop    = segBottom - (int16_t)segmentStep + 1;

      if (segTop < eqTopY) segTop = eqTopY;
      uint8_t segH = segBottom - segTop + 1;
      if (segH < 1) segH = 1;

      u8g2.drawBox(x, segTop, barWidth, segH);
    }

    // Peak – pojedyncza kreska nad słupkiem
    if (peakSeg > 0)
    {
      uint8_t ps = peakSeg - 1;
      int16_t peakBottom = eqBottomY - (int16_t)(ps * segmentStep);
      int16_t peakY      = peakBottom - 1;
      if (peakY >= eqTopY && peakY <= eqBottomY)
      {
        u8g2.drawBox(x, peakY, barWidth, 1);
      }
    }
  }

  // Komunikat o wyciszeniu – na środku
  if (volumeMute)
  {
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setDrawColor(0);
    u8g2.drawBox(60, (64/2) - 10, 256-120, 20);  // SCREEN_HEIGHT = 64, SCREEN_WIDTH = 256
    u8g2.setDrawColor(1);
    u8g2.setCursor((64/2) - 30, (64/2) + 4);  // SCREEN_HEIGHT = 64
    u8g2.print("MUTED");
  }

  u8g2.sendBuffer();
}

// ─────────────────────────────────────
// STYL 6 – cienkie kreski + peak + zegar
// ─────────────────────────────────────

void vuMeterMode6() // Tryb 6: 16 słupków z cienkich „kreseczek" + peak, pełny analizator segmentowy
{
  if (!eqAnalyzerEnabled)
  {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(10, 24);
    u8g2.print("ANALYZER OFF");
    u8g2.setCursor(10, 40);
    u8g2.print("Enable in Web UI");
    u8g2.sendBuffer();
    return;
  }

  // Sprawdź czy próbki napływają
  static uint32_t noSamplesTime6 = 0;
  if (!eq_analyzer_is_receiving_samples())
  {
    if (noSamplesTime6 == 0) noSamplesTime6 = millis();
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setCursor(10, 16);
    u8g2.print("NO AUDIO SAMPLES");
    u8g2.setCursor(10, 32);
    u8g2.print("Count: ");
    u8g2.print(eq_analyzer_get_sample_count());
    
    // Po 3 sekundach włącz generator testowy
    if (millis() - noSamplesTime6 > 3000) {
      u8g2.setCursor(10, 48);
      u8g2.print("Enabling test mode...");
      eq_analyzer_enable_test_generator(true);
      noSamplesTime6 = millis(); // Reset timer
    } else {
      u8g2.setCursor(10, 48);
      u8g2.print("Check audio source");
    }
    u8g2.sendBuffer();
    return;
  } else {
    noSamplesTime6 = 0; // Reset when receiving samples
    eq_analyzer_enable_test_generator(false); // Wyłącz generator gdy mamy audio
  }

  // 1. Pobranie poziomów z analizatora FFT (0..1)
  float levels[RUNTIME_EQ_BANDS];
  float peaks[RUNTIME_EQ_BANDS];
  eq_get_analyzer_levels(levels);
  eq_get_analyzer_peaks(peaks);

  for (uint8_t i = 0; i < EQ_BANDS && i < RUNTIME_EQ_BANDS; i++)
  {
    float lv = levels[i];
    float pk = peaks[i];
    if (lv < 0.0f) lv = 0.0f;
    if (lv > 1.0f) lv = 1.0f;
    if (pk < 0.0f) pk = 0.0f;
    if (pk > 1.0f) pk = 1.0f;

    eqLevel[i] = (uint8_t)(lv * 100.0f + 0.5f);
    eqPeak[i]  = (uint8_t)(pk * 100.0f + 0.5f);
  }

  // 2. Rysowanie – pasek z zegarem + stacja + głośnik u góry, cienkie słupki pod spodem
  u8g2.setDrawColor(1);
  u8g2.clearBuffer();

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

    uint8_t iconX = 256 - 40;  // SCREEN_WIDTH = 256
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

  // Ikonka głośnika po prawej
  uint8_t iconY = 2;
  uint8_t iconX = 256 - 40;  // SCREEN_WIDTH = 256

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

  u8g2.drawHLine(0, 13, 256);  // SCREEN_WIDTH = 256

  const uint8_t eqTopY      = 14;
  const uint8_t eqBottomY   = 64 - 1;  // SCREEN_HEIGHT = 64
  const uint8_t eqMaxHeight = eqBottomY - eqTopY + 1;

  const uint8_t maxSegments = eq6_maxSegments;
  const float segmentStep   = (float)eqMaxHeight / (float)maxSegments;

  // Auto-dopasowanie do pełnej szerokości ekranu
  eq_auto_fit_width(6, 256);
  
  const uint8_t barWidth = eq_barWidth6;
  const uint8_t barGap   = eq_barGap6;

  const uint16_t totalBarsWidth = EQ_BANDS * barWidth + (EQ_BANDS - 1) * barGap;
  int16_t startX = (256 - totalBarsWidth) / 2;  // SCREEN_WIDTH = 256
  if (startX < 2) startX = 2;  // Minimalny margines

  for (uint8_t i = 0; i < EQ_BANDS; i++)
  {
    uint8_t levelPercent = eqLevel[i];
    uint8_t peakPercent  = eqPeak[i];

    uint8_t segments = (levelPercent * maxSegments) / 100;
    if (segments > maxSegments) segments = maxSegments;

    uint8_t peakSeg = (peakPercent * maxSegments) / 100;
    if (peakSeg > maxSegments) peakSeg = maxSegments;

    int16_t x = startX + i * (barWidth + barGap);

    for (uint8_t s = 0; s < segments; s++)
    {
      int16_t segBottom = eqBottomY - (int16_t)(s * segmentStep);
      int16_t segTop    = segBottom - (int16_t)segmentStep + 1;
      
      if (segTop < eqTopY) segTop = eqTopY;
      if (segBottom < segTop) segBottom = segTop;
      
      uint8_t segHeight = segBottom - segTop + 1;
      if (segHeight > 0) {
        u8g2.drawBox(x, segTop, barWidth, segHeight);
      }
    }

    if (peakSeg > 0)
    {
      uint8_t ps = peakSeg - 1;
      int16_t peakBottom = eqBottomY - (int16_t)(ps * segmentStep);
      int16_t peakTop = peakBottom - 2;  // 2 piksele wysokości
      
      if (peakTop >= eqTopY && peakBottom <= eqBottomY) {
        u8g2.drawBox(x, peakTop, barWidth, 2);
      }
    }
  }

  if (volumeMute)
  {
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.setDrawColor(0);
    u8g2.drawBox(60, (64/2) - 10, 256-120, 20);  // SCREEN_HEIGHT = 64, SCREEN_WIDTH = 256
    u8g2.setDrawColor(1);
    u8g2.setCursor((64/2) - 30, (64/2) + 4);  // SCREEN_HEIGHT = 64
    u8g2.print("MUTED");
  }

  u8g2.sendBuffer();
}
