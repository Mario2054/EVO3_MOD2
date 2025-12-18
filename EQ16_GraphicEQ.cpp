#include "EQ16_GraphicEQ.h"
#include <FS.h>
#include <math.h>

// ============================================================================
// 16-BAND GRAPHIC EQUALIZER IMPLEMENTATION
// ============================================================================

// Static member definitions
EQ16_Band EQ16_GraphicEQ::bands[EQ16_BANDS];
bool EQ16_GraphicEQ::enabled = false;
bool EQ16_GraphicEQ::menuActive = false;
uint8_t EQ16_GraphicEQ::selectedBand = 0;
bool EQ16_GraphicEQ::settingsChanged = false;
unsigned long EQ16_GraphicEQ::lastSaveTime = 0;

// External functions and variables (from main.cpp)
extern fs::FS& getStorage();
extern bool eq16MenuActive;
extern U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2;

// ============================================================================
// INITIALIZATION
// ============================================================================

void EQ16_GraphicEQ::init() {
    Serial.println("EQ16: Initializing 16-Band Graphic Equalizer");
    
    // Initialize frequency bands (ISO standard + some additions)
    bands[0]  = {31.5f,   0, "31Hz",   0,0,0, 0,0, 0,0,0,0};     // Sub-bass
    bands[1]  = {63.0f,   0, "63Hz",   0,0,0, 0,0, 0,0,0,0};     // 
    bands[2]  = {125.0f,  0, "125Hz",  0,0,0, 0,0, 0,0,0,0};     // Bass
    bands[3]  = {250.0f,  0, "250Hz",  0,0,0, 0,0, 0,0,0,0};     //
    bands[4]  = {500.0f,  0, "500Hz",  0,0,0, 0,0, 0,0,0,0};     // Low-mid
    bands[5]  = {1000.0f, 0, "1kHz",   0,0,0, 0,0, 0,0,0,0};     // Midrange
    bands[6]  = {2000.0f, 0, "2kHz",   0,0,0, 0,0, 0,0,0,0};     //
    bands[7]  = {4000.0f, 0, "4kHz",   0,0,0, 0,0, 0,0,0,0};     // Upper-mid
    bands[8]  = {8000.0f, 0, "8kHz",   0,0,0, 0,0, 0,0,0,0};     // Presence
    bands[9]  = {16000.0f,0, "16kHz",  0,0,0, 0,0, 0,0,0,0};     // Brilliance
    bands[10] = {22.0f,   0, "22Hz",   0,0,0, 0,0, 0,0,0,0};     // Extended sub
    bands[11] = {355.0f,  0, "355Hz",  0,0,0, 0,0, 0,0,0,0};     // Lower-mid
    bands[12] = {710.0f,  0, "710Hz",  0,0,0, 0,0, 0,0,0,0};     // Mid detail
    bands[13] = {1420.0f, 0, "1.4kHz", 0,0,0, 0,0, 0,0,0,0};     // Upper-mid detail  
    bands[14] = {5660.0f, 0, "5.7kHz", 0,0,0, 0,0, 0,0,0,0};     // High-mid detail
    bands[15] = {11300.0f,0, "11kHz",  0,0,0, 0,0, 0,0,0,0};     // Air band
    
    // Calculate initial filter coefficients
    for (int i = 0; i < EQ16_BANDS; i++) {
        calculateFilterCoefficients(i);
    }
    
    // Try to load settings from SD card
    loadFromSD();
    
    Serial.println("EQ16: Initialization complete");
    printConfiguration();
}

void EQ16_GraphicEQ::enable(bool state) {
    enabled = state;
    settingsChanged = true;  // Zaznacz że ustawienia się zmieniły
    lastSaveTime = 0;        // Wymusimy natychmiastowy zapis przy następnym autoSave()
    
    Serial.printf("DEBUG EQ16: enable(%s) - settingsChanged=%s\n", state ? "true" : "false", settingsChanged ? "true" : "false");
    
    if (state) {
        Serial.println("EQ16: Enabled");
        // Recalculate coefficients in case settings changed
        for (int i = 0; i < EQ16_BANDS; i++) {
            calculateFilterCoefficients(i);
        }
        resetFilterStates();
    } else {
        Serial.println("EQ16: Disabled");
    }
}

// ============================================================================
// MENU CONTROL
// ============================================================================

void EQ16_GraphicEQ::setMenuActive(bool active) {
    if (active && !menuActive) {
        Serial.println("EQ16: Menu activated");
        menuActive = true;
        #ifdef EQ16_onMenuEnter
        EQ16_onMenuEnter();
        #endif
    } else if (!active && menuActive) {
        Serial.println("EQ16: Menu deactivated");
        menuActive = false;
        autoSave(); // Save on exit
        #ifdef EQ16_onMenuExit  
        EQ16_onMenuExit();
        #endif
    }
}

// ============================================================================
// BAND SELECTION
// ============================================================================

void EQ16_GraphicEQ::selectNextBand() {
    selectedBand = (selectedBand + 1) % EQ16_BANDS;
    Serial.printf("EQ16: Selected band %d (%s - %.1fHz)\n", 
                  selectedBand + 1, 
                  bands[selectedBand].name,
                  bands[selectedBand].frequency);
}

void EQ16_GraphicEQ::selectPrevBand() {
    selectedBand = (selectedBand == 0) ? (EQ16_BANDS - 1) : (selectedBand - 1);
    Serial.printf("EQ16: Selected band %d (%s - %.1fHz)\n", 
                  selectedBand + 1, 
                  bands[selectedBand].name,
                  bands[selectedBand].frequency);
}

const char* EQ16_GraphicEQ::getBandName(uint8_t band) {
    if (band >= EQ16_BANDS) return "Invalid";
    return bands[band].name;
}

float EQ16_GraphicEQ::getBandFrequency(uint8_t band) {
    if (band >= EQ16_BANDS) return 0.0f;
    return bands[band].frequency;
}

// ============================================================================
// GAIN CONTROL
// ============================================================================

void EQ16_GraphicEQ::increaseBandGain() {
    if (bands[selectedBand].gain < EQ16_GAIN_MAX) {
        bands[selectedBand].gain++;
        calculateFilterCoefficients(selectedBand);
        settingsChanged = true;
        
        Serial.printf("EQ16: Band %d (%s) gain: %+d dB\n", 
                      selectedBand + 1, 
                      bands[selectedBand].name, 
                      bands[selectedBand].gain);
                      
        #ifdef EQ16_onSettingsChanged
        EQ16_onSettingsChanged();
        #endif
    }
}

void EQ16_GraphicEQ::decreaseBandGain() {
    if (bands[selectedBand].gain > EQ16_GAIN_MIN) {
        bands[selectedBand].gain--;
        calculateFilterCoefficients(selectedBand);
        settingsChanged = true;
        
        Serial.printf("EQ16: Band %d (%s) gain: %+d dB\n", 
                      selectedBand + 1, 
                      bands[selectedBand].name, 
                      bands[selectedBand].gain);
                      
        #ifdef EQ16_onSettingsChanged
        EQ16_onSettingsChanged();
        #endif
    }
}

int8_t EQ16_GraphicEQ::getBandGain(uint8_t band) {
    if (band >= EQ16_BANDS) return 0;
    return bands[band].gain;
}

void EQ16_GraphicEQ::setBandGain(uint8_t band, int8_t gain) {
    if (band >= EQ16_BANDS) return;
    
    gain = constrain(gain, EQ16_GAIN_MIN, EQ16_GAIN_MAX);
    if (bands[band].gain != gain) {
        bands[band].gain = gain;
        calculateFilterCoefficients(band);
        settingsChanged = true;
    }
}

int8_t* EQ16_GraphicEQ::getAllGains() {
    static int8_t gains[EQ16_BANDS];
    for (int i = 0; i < EQ16_BANDS; i++) {
        gains[i] = bands[i].gain;
    }
    return gains;
}

// ============================================================================
// PRESETS
// ============================================================================

void EQ16_GraphicEQ::resetAllBands() {
    Serial.println("EQ16: Resetting all bands to 0 dB");
    for (int i = 0; i < EQ16_BANDS; i++) {
        bands[i].gain = 0;
        calculateFilterCoefficients(i);
    }
    resetFilterStates();
    settingsChanged = true;
}

void EQ16_GraphicEQ::loadPreset(uint8_t presetId) {
    Serial.printf("EQ16: Loading preset %d\n", presetId);
    
    // Reset first
    resetAllBands();
    
    switch (presetId) {
        case 0: // Flat
            // Already reset to 0
            break;
            
        case 1: // Bass Boost
            setBandGain(0, 4);   // 31Hz
            setBandGain(1, 3);   // 63Hz  
            setBandGain(2, 2);   // 125Hz
            setBandGain(3, 1);   // 250Hz
            break;
            
        case 2: // Vocal
            setBandGain(4, 2);   // 500Hz
            setBandGain(5, 3);   // 1kHz
            setBandGain(6, 3);   // 2kHz
            setBandGain(7, 2);   // 4kHz
            break;
            
        case 3: // Presence
            setBandGain(7, 3);   // 4kHz
            setBandGain(8, 4);   // 8kHz
            setBandGain(9, 3);   // 16kHz
            break;
            
        case 4: // V-Shape
            setBandGain(0, 3);   // 31Hz
            setBandGain(1, 2);   // 63Hz
            setBandGain(8, 2);   // 8kHz
            setBandGain(9, 3);   // 16kHz
            break;
    }
    
    settingsChanged = true;
}

// ============================================================================
// IIR FILTER CALCULATION (PEAKING EQ)
// ============================================================================

void EQ16_GraphicEQ::calculateFilterCoefficients(uint8_t bandIndex) {
    if (bandIndex >= EQ16_BANDS) return;
    
    EQ16_Band* band = &bands[bandIndex];
    
    // If gain is 0, set to unity (no filtering)
    if (band->gain == 0) {
        band->b0 = 1.0f;
        band->b1 = 0.0f;
        band->b2 = 0.0f;
        band->a1 = 0.0f;
        band->a2 = 0.0f;
        return;
    }
    
    // Peaking EQ filter coefficients
    float sampleRate = 44100.0f; // Assume 44.1kHz
    float frequency = band->frequency;
    float gainLinear = pow(10.0f, band->gain / 20.0f); // dB to linear
    float Q = 0.707f; // Quality factor (bandwidth)
    
    float omega = 2.0f * M_PI * frequency / sampleRate;
    float sin_omega = sin(omega);
    float cos_omega = cos(omega);
    float alpha = sin_omega / (2.0f * Q);
    float A = gainLinear;
    
    // Peaking EQ coefficients
    float norm = 1.0f + alpha / A;
    band->b0 = (1.0f + alpha * A) / norm;
    band->b1 = (-2.0f * cos_omega) / norm;
    band->b2 = (1.0f - alpha * A) / norm;
    band->a1 = (-2.0f * cos_omega) / norm;
    band->a2 = (1.0f - alpha / A) / norm;
}

void EQ16_GraphicEQ::resetFilterStates() {
    for (int i = 0; i < EQ16_BANDS; i++) {
        bands[i].x1 = bands[i].x2 = 0.0f;
        bands[i].y1 = bands[i].y2 = 0.0f;
    }
}

// ============================================================================
// AUDIO PROCESSING
// ============================================================================

float EQ16_GraphicEQ::processSample(float sample, uint8_t bandIndex) {
    if (bandIndex >= EQ16_BANDS) return sample;
    
    EQ16_Band* band = &bands[bandIndex];
    
    // Biquad IIR filter processing
    float output = band->b0 * sample + 
                   band->b1 * band->x1 + 
                   band->b2 * band->x2 - 
                   band->a1 * band->y1 - 
                   band->a2 * band->y2;
    
    // Update delay line
    band->x2 = band->x1;
    band->x1 = sample;
    band->y2 = band->y1;
    band->y1 = output;
    
    return output;
}

float EQ16_GraphicEQ::processSampleStereo(float sample, float rightSample, bool isLeft) {
    if (!enabled) return sample;
    
    float output = sample;
    
    // Process through all bands sequentially
    for (int i = 0; i < EQ16_BANDS; i++) {
        if (bands[i].gain != 0) { // Skip bands with 0 gain for efficiency
            output = processSample(output, i);
        }
    }
    
    return output;
}

void EQ16_GraphicEQ::processStereo(int16_t* leftSamples, int16_t* rightSamples, size_t sampleCount) {
    if (!enabled || sampleCount == 0) return;
    
    for (size_t i = 0; i < sampleCount; i++) {
        // Convert to float
        float leftFloat = leftSamples[i] / 32768.0f;
        float rightFloat = rightSamples[i] / 32768.0f;
        
        // Process both channels
        leftFloat = processSampleStereo(leftFloat, rightFloat, true);
        rightFloat = processSampleStereo(rightFloat, leftFloat, false);
        
        // Convert back to int16
        leftSamples[i] = (int16_t)(leftFloat * 32767.0f);
        rightSamples[i] = (int16_t)(rightFloat * 32767.0f);
    }
}

// ============================================================================
// DISPLAY
// ============================================================================

void EQ16_GraphicEQ::displayMenu() {
    Serial.printf("DEBUG EQ16: displayMenu() called - menuActive=%s, eq16MenuActive=%s\n",
                  menuActive ? "true" : "false", 
                  eq16MenuActive ? "true" : "false");
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    
    // Title
    u8g2.setFont(u8g2_font_6x10_mf);
    u8g2.drawStr(85, 10, "16-BAND EQUALIZER");
    
    // Current band info (top line)
    uint8_t band = selectedBand;
    int8_t gain = bands[band].gain;
    
    u8g2.setCursor(0, 10);
    u8g2.printf("%s", bands[band].name);
    
    u8g2.setCursor(200, 10);
    if (gain >= 0) {
        u8g2.printf("+%ddB", gain);
    } else {
        u8g2.printf("%ddB", gain);
    }
    
    // Draw vertical sliders with frequency labels
    int barWidth = 240 / 16; // ~15 pixels per band  
    int maxBarHeight = 30;    // Maximum bar height
    int baseY = 45;           // Center line for 0dB
    
    for (int i = 0; i < EQ16_BANDS; i++) {
        int x = i * barWidth;
        int barHeight = (abs(bands[i].gain) * maxBarHeight) / EQ16_GAIN_MAX; // Scale to max height
        
        // Draw gain bar with darker/enhanced effect (positive up, negative down)
        u8g2.setDrawColor(1);
        
        if (bands[i].gain >= 0) {
            // Wypełniony słupek
            u8g2.drawBox(x + 2, baseY - barHeight, barWidth - 4, barHeight);
            // Dodaj obramowanie dla ciemniejszego efektu
            if (barHeight > 0) {
                u8g2.drawFrame(x + 2, baseY - barHeight, barWidth - 4, barHeight);
                // Dodaj szczytowy znacznik (ciemniejszy)
                u8g2.drawBox(x + 1, baseY - barHeight - 1, barWidth - 2, 2); // Szerszy szczyt
            }
        } else {
            // Wypełniony słupek
            u8g2.drawBox(x + 2, baseY, barWidth - 4, barHeight);
            // Dodaj obramowanie dla ciemniejszego efektu
            if (barHeight > 0) {
                u8g2.drawFrame(x + 2, baseY, barWidth - 4, barHeight);
                // Dodaj szczytowy znacznik (ciemniejszy) dla wartości ujemnych
                u8g2.drawBox(x + 1, baseY + barHeight - 1, barWidth - 2, 2); // Szerszy szczyt
            }
        }
        
        // Highlight selected band with frame
        if (i == selectedBand) {
            u8g2.drawFrame(x + 1, baseY - maxBarHeight - 2, barWidth - 2, maxBarHeight * 2 + 4);
        }
        
        // Draw frequency labels below sliders (every 2nd band to avoid overlap)
        if (i % 2 == 0 || i == selectedBand) {
            u8g2.setFont(u8g2_font_4x6_mf);
            
            // Short frequency labels
            const char* shortLabels[] = {
                "31", "63", "125", "250", "500", "1k", "2k", "4k", 
                "8k", "16k", "22", "355", "710", "1.4", "5.7", "11k"
            };
            
            int textWidth = strlen(shortLabels[i]) * 4;
            int textX = x + (barWidth - textWidth) / 2;
            u8g2.drawStr(textX, 64, shortLabels[i]);
        }
    }
    
    // Draw center line (0dB reference)
    u8g2.setDrawColor(1);
    u8g2.drawHLine(0, baseY, 240);
    
    // Draw +/- dB markers on left side
    u8g2.setFont(u8g2_font_4x6_mf);
    u8g2.drawStr(0, baseY - maxBarHeight + 3, "+12");
    u8g2.drawStr(0, baseY + 3, "0");
    u8g2.drawStr(0, baseY + maxBarHeight + 3, "-12");
    
    // Mode indicator
    u8g2.setFont(u8g2_font_5x7_mf);
    u8g2.setCursor(200, 25);
    if (menuActive) {
        if (selectedBand < 10) {  // Use different mode indicator logic
            u8g2.print("BAND");
        } else {
            u8g2.print("GAIN"); 
        }
    }
    
    // Status
    u8g2.setCursor(0, 25);
    u8g2.printf("EQ:%s", enabled ? "ON" : "OFF");
    
    u8g2.sendBuffer();
}

// ============================================================================
// STORAGE
// ============================================================================

void EQ16_GraphicEQ::saveToSD() {
    Serial.println("DEBUG EQ16: saveToSD() called");
    fs::FS& storage = getStorage();
    
    File file = storage.open("/eq16_settings.txt", "w");
    if (!file) {
        Serial.println("EQ16: Failed to open settings file for writing");
        return;
    }
    
    file.println("# EQ16 Settings");
    file.printf("enabled=%d\n", enabled ? 1 : 0);
    
    for (int i = 0; i < EQ16_BANDS; i++) {
        file.printf("band_%d=%d\n", i, bands[i].gain);
    }
    
    file.close();
    settingsChanged = false;
    lastSaveTime = millis();
    
    Serial.printf("DEBUG EQ16: Settings saved to SD card - enabled=%s\n", enabled ? "true" : "false");
}

void EQ16_GraphicEQ::loadFromSD() {
    fs::FS& storage = getStorage();
    
    if (!storage.exists("/eq16_settings.txt")) {
        Serial.println("EQ16: No settings file found, using defaults");
        return;
    }
    
    File file = storage.open("/eq16_settings.txt", "r");
    if (!file) {
        Serial.println("EQ16: Failed to open settings file for reading");
        return;
    }
    
    String line;
    while (file.available()) {
        line = file.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("#")) continue; // Comment
        
        int equalPos = line.indexOf('=');
        if (equalPos > 0) {
            String key = line.substring(0, equalPos);
            String value = line.substring(equalPos + 1);
            
            if (key == "enabled") {
                enabled = value.toInt() != 0;
            } else if (key.startsWith("band_")) {
                int bandIndex = key.substring(5).toInt();
                if (bandIndex >= 0 && bandIndex < EQ16_BANDS) {
                    int gain = constrain(value.toInt(), EQ16_GAIN_MIN, EQ16_GAIN_MAX);
                    bands[bandIndex].gain = gain;
                    calculateFilterCoefficients(bandIndex);
                }
            }
        }
    }
    
    file.close();
    settingsChanged = false;
    
    Serial.println("EQ16: Settings loaded from SD card");
}

void EQ16_GraphicEQ::autoSave() {
    if (settingsChanged && (millis() - lastSaveTime > 5000)) { // Save every 5 seconds
        saveToSD();
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void EQ16_GraphicEQ::printConfiguration() {
    Serial.println("EQ16: Current Configuration");
    Serial.printf("Enabled: %s\n", enabled ? "Yes" : "No");
    Serial.printf("Menu Active: %s\n", menuActive ? "Yes" : "No");
    Serial.printf("Selected Band: %d (%s)\n", selectedBand + 1, bands[selectedBand].name);
    Serial.println("Band Configuration:");
    
    for (int i = 0; i < EQ16_BANDS; i++) {
        Serial.printf("  %2d: %6s %8.1fHz %+3ddB\n", 
                      i + 1, 
                      bands[i].name, 
                      bands[i].frequency, 
                      bands[i].gain);
    }
}

void EQ16_GraphicEQ::printBandInfo(uint8_t band) {
    if (band >= EQ16_BANDS) return;
    
    Serial.printf("EQ16 Band %d Info:\n", band + 1);
    Serial.printf("  Name: %s\n", bands[band].name);
    Serial.printf("  Frequency: %.1f Hz\n", bands[band].frequency);
    Serial.printf("  Gain: %+d dB\n", bands[band].gain);
    Serial.printf("  Coefficients: b0=%.4f, b1=%.4f, b2=%.4f, a1=%.4f, a2=%.4f\n",
                  bands[band].b0, bands[band].b1, bands[band].b2, 
                  bands[band].a1, bands[band].a2);
}

// ============================================================================
// GLOBAL FUNCTION WRAPPERS
// ============================================================================

void EQ16_init() { EQ16_GraphicEQ::init(); }
void EQ16_enable(bool state) { EQ16_GraphicEQ::enable(state); }
bool EQ16_isEnabled() { return EQ16_GraphicEQ::isEnabled(); }
void EQ16_displayMenu() { EQ16_GraphicEQ::displayMenu(); }
void EQ16_setMenuActive(bool active) { EQ16_GraphicEQ::setMenuActive(active); }
bool EQ16_isMenuActive() { return EQ16_GraphicEQ::isMenuActive(); }
void EQ16_selectNextBand() { EQ16_GraphicEQ::selectNextBand(); }
void EQ16_selectPrevBand() { EQ16_GraphicEQ::selectPrevBand(); }
void EQ16_increaseBandGain() { EQ16_GraphicEQ::increaseBandGain(); }
void EQ16_decreaseBandGain() { EQ16_GraphicEQ::decreaseBandGain(); }
void EQ16_resetAllBands() { EQ16_GraphicEQ::resetAllBands(); }
void EQ16_loadPreset(uint8_t presetId) { EQ16_GraphicEQ::loadPreset(presetId); }
void EQ16_saveToSD() { EQ16_GraphicEQ::saveToSD(); }
void EQ16_loadFromSD() { EQ16_GraphicEQ::loadFromSD(); }
void EQ16_autoSave() { EQ16_GraphicEQ::autoSave(); }
void EQ16_processStereo(int16_t* left, int16_t* right, size_t count) { 
    EQ16_GraphicEQ::processStereo(left, right, count); 
}
float EQ16_processSample(float leftSample, float rightSample, bool isLeft) {
    return EQ16_GraphicEQ::processSampleStereo(leftSample, rightSample, isLeft);
}