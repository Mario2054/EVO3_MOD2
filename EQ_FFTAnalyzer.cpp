#include "EQ_FFTAnalyzer.h"

// Definicja lokalna EQ_BANDS (taka sama jak w main.cpp)
static const uint8_t EQ_BANDS = 16;

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>

bool eqAnalyzerEnabled = false;
uint8_t eq6_maxSegments = 16;  // Zmniejszone dla lepszej widoczności
uint8_t eq5_maxSegments = 20;  // Segmenty dla trybu 5
uint8_t eq_barWidth5 = 12;     // Szerokość pasków tryb 5
uint8_t eq_barGap5 = 4;        // Odstęp między paskami tryb 5
uint8_t eq_barWidth6 = 8;      // Szerokość pasków tryb 6
uint8_t eq_barGap6 = 3;        // Odstęp między paskami tryb 6

// Diagnostyczne - licznik próbek otrzymanych
static uint32_t g_sampleCount = 0;
static uint32_t g_lastSampleCount = 0;
static uint32_t g_lastFrameProcessed = 0;  // Czas ostatniej przetworzonej ramki

// Test generator - symuluje audio gdy brak prawdziwego sygnału
static bool g_useTestGenerator = false;
static uint32_t g_testPhase = 0;

// ====================== TUNING (CPU / jakość) ======================
static uint32_t g_audioSampleRate = 44100;  // Dynamiczna częstotliwość próbkowania
static constexpr uint32_t DEFAULT_SAMPLE_RATE = 44100;

// CPU: 2 = co druga próbka, 4 = co czwarta (mniej CPU, mniej detali)
static constexpr uint8_t  DOWNSAMPLE = 2;

static uint32_t get_effective_sample_rate() {
  return g_audioSampleRate / DOWNSAMPLE;
}

// Jakość/CPU: 256/384/512 (256 szybsze, mniej RAM, lepsze dla real-time)
static constexpr uint16_t N = 256;

// CPU: analizuj nie częściej niż co X ms (40ms ~ 25 fps)
static constexpr uint32_t ANALYZE_PERIOD_MS = 40;

// Kolejka próbek mono do taska analizatora
static QueueHandle_t g_q = nullptr;

static float g_level[RUNTIME_EQ_BANDS] = {0};
static float g_peak[RUNTIME_EQ_BANDS]  = {0};
static uint32_t g_peakHoldUntil[RUNTIME_EQ_BANDS] = {0};

// Dynamika (możesz podkręcić pod siebie)
static constexpr float LEVEL_RISE = 0.55f;    // szybko rośnie
static constexpr float LEVEL_FALL = 0.10f;    // jak szybko opada
static constexpr float PEAK_FALL  = 0.06f;    // jak opada peak
static constexpr uint32_t PEAK_HOLD_MS = 200; // jak długo trzyma peak

// 16 pasm – rozłożone logarytmicznie (jak "prawdziwy" analizator)
static const float kBandHz[RUNTIME_EQ_BANDS] = {
  60, 90, 130, 180,
  250, 350, 500, 700,
  1000, 1400, 2000, 2800,
  4000, 5600, 8000, 12000
};

static float goertzel_mag(const int16_t* x, int n, float freqHz)
{
  uint32_t fs_hz = get_effective_sample_rate();
  float kf = (freqHz * (float)n) / (float)fs_hz;
  int k = (int)(kf + 0.5f);
  if (k < 1) k = 1;
  if (k > (n/2 - 1)) k = (n/2 - 1);

  float omega  = (2.0f * 3.14159265f * (float)k) / (float)n;
  float cosine = cosf(omega);
  float coeff  = 2.0f * cosine;

  float q0 = 0, q1 = 0, q2 = 0;
  for (int i = 0; i < n; i++) {
    q0 = coeff * q1 - q2 + (float)x[i];
    q2 = q1;
    q1 = q0;
  }

  float real = q1 - q2 * cosine;
  float imag = q2 * sinf(omega);
  return sqrtf(real*real + imag*imag);
}

static void analyze_frame(const int16_t* x)
{
  // Auto-referencja głośności ramki (żeby nie kalibrować na każdą stację)
  double sumAbs = 0;
  for (int i=0;i<N;i++) sumAbs += fabs((double)x[i]);
  float ref = (float)(sumAbs / (double)N);
  if (ref < 50.0f) ref = 50.0f;

  uint32_t now = millis();

  // Debug co 3 sekundy - pokazuj wartości mag i ref
  static uint32_t lastAnalyzeDebug = 0;
  bool showDebug = (now - lastAnalyzeDebug > 3000);
  if (showDebug) {
    Serial.print("Analyzer ref=");
    Serial.print(ref, 1);
    Serial.print(" mags: ");
    lastAnalyzeDebug = now;
  }

  for (int b=0;b<RUNTIME_EQ_BANDS;b++) {
    float mag = goertzel_mag(x, N, kBandHz[b]);

    // POPRAWIONE WZMOCNIENIE DLA WYSOKICH CZĘSTOTLIWOŚCI - różne dla każdego pasma
    float freqGain;
    float freq = kBandHz[b];
    
    if (freq < 100.0f) {
        freqGain = 200.0f;      // Basy - większe wzmocnienie
    } else if (freq < 250.0f) {
        freqGain = 180.0f;      // Niskie - duże wzmocnienie
    } else if (freq < 500.0f) {
        freqGain = 160.0f;      // Niski środek
    } else if (freq < 1000.0f) {
        freqGain = 140.0f;      // Środek
    } else if (freq < 2000.0f) {
        freqGain = 120.0f;      // Górny środek
    } else if (freq < 4000.0f) {
        freqGain = 70.0f;       // Wysokie - MOCNIEJSZE WZMOCNIENIE dla prawej strony (+43%)
    } else if (freq < 8000.0f) {
        freqGain = 50.0f;       // Bardzo wysokie - JESZCZE MOCNIEJSZE (+60%)
    } else {
        freqGain = 30.0f;       // Ultra wysokie - MAKSYMALNE WZMOCNIENIE dla prawej strony (+100%)
    }
    
    float v = mag / (ref * freqGain);

    if (v < 0) v = 0;
    if (v > 1) v = 1;

    // Debug tylko kilka pierwszych pasm
    if (showDebug && b < 4) {
      Serial.print(mag, 0);
      Serial.print(":");
      Serial.print(v, 2);
      Serial.print(" ");
    }

    // Wygładzanie poziomu - lepsze dla wysokich częstotliwości
    float riseSpeed = LEVEL_RISE;
    float fallSpeed = LEVEL_FALL;
    
    // Dla wysokich częstotliwości (prawej strony) - szybsza reakcja
    if (freq > 2000.0f) {
        riseSpeed = LEVEL_RISE * 1.2f;  // 20% szybsze narastanie
        fallSpeed = LEVEL_FALL * 1.5f;  // 50% szybsze opadanie dla lepszej responsywności
    }
    
    if (v > g_level[b]) g_level[b] = g_level[b] + (v - g_level[b]) * riseSpeed;
    else                g_level[b] = g_level[b] + (v - g_level[b]) * fallSpeed;

    // Peak hold
    if (g_level[b] >= g_peak[b]) {
      g_peak[b] = g_level[b];
      g_peakHoldUntil[b] = now + PEAK_HOLD_MS;
    } else if (now >= g_peakHoldUntil[b]) {
      g_peak[b] = g_peak[b] + (g_level[b] - g_peak[b]) * PEAK_FALL;
      if (g_peak[b] < 0) g_peak[b] = 0;
    }
  }

  if (showDebug) {
    Serial.println();
  }
}

// Generator testowy - tworzy przykładowy sygnał audio
static void generate_test_samples()
{
  if (!g_useTestGenerator || !g_q) return;
  
  // Generuj kilka próbek z różnymi częstotliwościami
  for (int i = 0; i < 64; i++) {
    g_testPhase++;
    // Symulja mix kilku częstotliwości
    float f1 = sin(g_testPhase * 0.01f) * 500.0f;      // 100 Hz
    float f2 = sin(g_testPhase * 0.02f) * 300.0f;      // 400 Hz  
    float f3 = sin(g_testPhase * 0.05f) * 200.0f;      // 1kHz
    int16_t sample = (int16_t)(f1 + f2 + f3);
    xQueueSend(g_q, &sample, 0);
  }
  g_sampleCount += 64; // Zaktualizuj licznik dla diagnostyki
}

static void analyzer_task(void*)
{
  static int16_t frame[N];
  uint32_t lastAnalyze = 0;
  uint32_t lastTestGen = 0;
  uint32_t taskLoopCount = 0;

  Serial.println("Analyzer task started!");

  for (;;) {
    taskLoopCount++;
    
    if (!eqAnalyzerEnabled) {
      if (taskLoopCount % 100 == 0) {
        Serial.println("Task: analyzer disabled, waiting...");
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    if (!g_q) {
      Serial.println("Task: g_q is NULL!");
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    // Sprawdź czy przez ostatnie 2 sekundy otrzymywaliśmy próbki
    uint32_t now = millis();
    if (g_sampleCount == g_lastSampleCount && (now - lastTestGen > 2000)) {
      g_useTestGenerator = true; // Włącz generator testowy
      lastTestGen = now;
    } else if (g_sampleCount != g_lastSampleCount) {
      g_useTestGenerator = false; // Wyłącz gdy mamy prawdziwe audio
      g_lastSampleCount = g_sampleCount;
    }
    
    // Generuj testowe próbki co 50ms jeśli potrzeba
    if (g_useTestGenerator && (now - lastTestGen > 50)) {
      generate_test_samples();
      lastTestGen = now;
    }

    int got = 0;
    while (got < N) {
      int16_t s;
      if (xQueueReceive(g_q, &s, pdMS_TO_TICKS(20)) == pdTRUE) frame[got++] = s;
      else break;
    }

    if (got == N) {
      uint32_t now = millis();
      if (now - lastAnalyze >= ANALYZE_PERIOD_MS) {
        lastAnalyze = now;
        analyze_frame(frame);
        g_lastFrameProcessed = now;  // Zapisz czas przetworzenia ramki
        
        // Debug co 5 sekund
        static uint32_t lastDebug = 0;
        if (now - lastDebug > 5000) {
          Serial.print("Analyzer: processed frame, got ");
          Serial.print(got);
          Serial.println(" samples");
          lastDebug = now;
        }
      }
    } else {
      // Debug jeśli nie ma wystarczająco próbek
      static uint32_t noSamplesDebug = 0;
      if (taskLoopCount % 1000 == 0) {
        Serial.print("Task: only got ");
        Serial.print(got);
        Serial.print(" samples (need ");
        Serial.print(N);
        Serial.println(")");
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

// ==================== HOOK Z AUDIO ====================
// Jeśli słupki stoją, prawie zawsze biblioteka audio NIE wywołuje tego hooka.
void audio_process_i2s(int16_t* outBuff, int32_t validSamples, bool* continueI2S)
{
  // WAŻNE: continueI2S musi być ustawione na true żeby audio kontynuowało
  if (continueI2S) *continueI2S = true;
  
  // Debug - sprawdź czy hook jest w ogóle wywoływany
  static uint32_t hookCallCount = 0;
  hookCallCount++;
  if (hookCallCount % 1000 == 0) {
    Serial.print("audio_process_i2s called: ");
    Serial.print(hookCallCount);
    Serial.print(" times, validSamples: ");
    Serial.println(validSamples);
  }
  
  // Debug - sprawdź stan analizatora
  if (!eqAnalyzerEnabled) {
    static uint32_t disabledCount = 0;
    disabledCount++;
    if (disabledCount % 5000 == 0) {
      Serial.println("Analyzer DISABLED - enable it first!");
    }
    return;
  }
  
  if (!g_q) {
    Serial.println("Queue g_q is NULL - call eq_analyzer_init() first!");
    return;
  }

  // Diagnostyka - zlicz próbki
  g_sampleCount++;
  
  // stereo interleaved: L,R,L,R...
  int32_t samplesAdded = 0;
  for (int32_t i = 0; i + 1 < validSamples; i += (int32_t)(2 * DOWNSAMPLE)) {
    int32_t L = outBuff[i];
    int32_t R = outBuff[i + 1];
    int16_t mono = (int16_t)((L + R) / 2);
    if (xQueueSend(g_q, &mono, 0) == pdTRUE) {
      samplesAdded++;
    }
  }
  
  // Debug co 10000 wywołań
  if (hookCallCount % 10000 == 0) {
    UBaseType_t queueSpaces = uxQueueSpacesAvailable(g_q);
    Serial.print("Queue spaces available: ");
    Serial.print(queueSpaces);
    Serial.print(", samples added this call: ");
    Serial.println(samplesAdded);
  }
}

void eq_analyzer_init(void)
{
  if (!g_q) {
    g_q = xQueueCreate(4096, sizeof(int16_t));
    if (!g_q) return;
    xTaskCreatePinnedToCore(analyzer_task, "analyzer", 8192, nullptr, 1, nullptr, 1);
  }
}

void eq_analyzer_reset(void)
{
  for (int i=0;i<RUNTIME_EQ_BANDS;i++) { g_level[i]=0; g_peak[i]=0; g_peakHoldUntil[i]=0; }
  if (g_q) xQueueReset(g_q);
}

void eq_analyzer_set_enabled(bool enabled)
{
  eqAnalyzerEnabled = enabled;
  if (!enabled) eq_analyzer_reset();
}

void eq_get_analyzer_levels(float* outLevels)
{
  for (int i=0;i<RUNTIME_EQ_BANDS;i++) outLevels[i] = g_level[i];
}

void eq_get_analyzer_peaks(float* outPeaks)
{
  for (int i=0;i<RUNTIME_EQ_BANDS;i++) outPeaks[i] = g_peak[i];
}

// Funkcja diagnostyczna - sprawdź czy próbki napływają
bool eq_analyzer_is_receiving_samples()
{
  static uint32_t lastDebug = 0;
  uint32_t now = millis();
  
  // Sprawdź czy ramka była przetworzona w ostatnich 3 sekundach
  bool recentlyProcessed = (now - g_lastFrameProcessed) < 3000;
  
  // Alternatywnie sprawdź czy próbki napływają
  uint32_t current = g_sampleCount;
  bool samplesIncreasing = (current != g_lastSampleCount);
  
  bool receiving = recentlyProcessed || samplesIncreasing;
  
  // Debug co 3 sekundy
  if (now - lastDebug > 3000) {
    Serial.print("eq_analyzer_is_receiving_samples: recentFrame=");
    Serial.print(recentlyProcessed ? "YES" : "NO");
    Serial.print(", samplesInc=");
    Serial.print(samplesIncreasing ? "YES" : "NO");
    Serial.print(", result=");
    Serial.print(receiving ? "YES" : "NO");
    
    if (g_q) {
      UBaseType_t count = uxQueueMessagesWaiting(g_q);
      Serial.print(", queue=");
      Serial.print(count);
    }
    Serial.println();
    lastDebug = now;
  }
  
  g_lastSampleCount = current;
  return receiving;
}

uint32_t eq_analyzer_get_sample_count()
{
  return g_sampleCount;
}

void eq_analyzer_enable_test_generator(bool enable)
{
  g_useTestGenerator = enable;
  if (enable) {
    g_testPhase = 0;
  }
}

void eq_analyzer_set_sample_rate(uint32_t sampleRate)
{
  if (sampleRate >= 8000 && sampleRate <= 192000) {
    g_audioSampleRate = sampleRate;
    Serial.printf("[ANALYZER] Sample rate set to: %u Hz (effective: %u Hz)\n", 
                  g_audioSampleRate, get_effective_sample_rate());
  } else {
    Serial.printf("[ANALYZER] Invalid sample rate: %u Hz, keeping: %u Hz\n", 
                  sampleRate, g_audioSampleRate);
  }
}

uint32_t eq_analyzer_get_sample_rate()
{
  return g_audioSampleRate;
}

void eq_analyzer_print_diagnostics()
{
  Serial.println("\n=== ANALYZER DIAGNOSTICS ===");
  Serial.printf("Enabled: %s\n", eqAnalyzerEnabled ? "YES" : "NO");
  Serial.printf("Sample Rate: %u Hz (effective: %u Hz)\n", g_audioSampleRate, get_effective_sample_rate());
  Serial.printf("Total Samples Received: %u\n", g_sampleCount);
  Serial.printf("Test Generator: %s\n", g_useTestGenerator ? "ACTIVE" : "OFF");
  
  if (g_q) {
    UBaseType_t queueCount = uxQueueMessagesWaiting(g_q);
    Serial.printf("Queue Status: %u/%u samples\n", queueCount, N);
  } else {
    Serial.println("Queue Status: NOT INITIALIZED");
  }
  
  Serial.print("Current Levels: ");
  for (int i = 0; i < RUNTIME_EQ_BANDS; i++) {
    Serial.printf("%.2f ", g_level[i]);
    if (i == 7) Serial.print("\n                ");
  }
  Serial.println();
  
  Serial.print("Band Frequencies: ");
  for (int i = 0; i < RUNTIME_EQ_BANDS; i++) {
    Serial.printf("%.0fHz ", kBandHz[i]);
    if (i == 7) Serial.print("\n                  ");
  }
  Serial.println();
  Serial.println("============================\n");
}

// Funkcje konfiguracji analizatora
void eq_set_style5_params(uint8_t segments, uint8_t barWidth, uint8_t barGap)
{
  if (segments > 0 && segments <= 40) eq5_maxSegments = segments;
  if (barWidth > 0 && barWidth <= 20) eq_barWidth5 = barWidth;  
  if (barGap >= 0 && barGap <= 10) eq_barGap5 = barGap;
}

void eq_set_style6_params(uint8_t segments, uint8_t barWidth, uint8_t barGap)
{
  if (segments > 0 && segments <= 40) eq6_maxSegments = segments;
  if (barWidth > 0 && barWidth <= 20) eq_barWidth6 = barWidth;
  if (barGap >= 0 && barGap <= 10) eq_barGap6 = barGap;
}

// Funkcja automatycznego dopasowania szerokości do całego ekranu
void eq_auto_fit_width(uint8_t style, uint16_t screenWidth)
{
  if (style == 5) {
    // Oblicz optymalne rozmiary dla trybu 5
    uint8_t totalGaps = (EQ_BANDS - 1) * eq_barGap5;
    uint8_t availableWidth = screenWidth - totalGaps - 4; // -4 margin
    uint8_t optimalBarWidth = availableWidth / EQ_BANDS;
    if (optimalBarWidth > 0) {
      eq_barWidth5 = optimalBarWidth;
    }
  } else if (style == 6) {
    // Oblicz optymalne rozmiary dla trybu 6
    uint8_t totalGaps = (EQ_BANDS - 1) * eq_barGap6;
    uint8_t availableWidth = screenWidth - totalGaps - 4; // -4 margin
    uint8_t optimalBarWidth = availableWidth / EQ_BANDS;
    if (optimalBarWidth > 0) {
      eq_barWidth6 = optimalBarWidth;
    }
  }
}
