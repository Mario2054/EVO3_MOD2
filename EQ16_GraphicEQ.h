#ifndef EQ16_GRAPHICEQ_H
#define EQ16_GRAPHICEQ_H

#include <Arduino.h>
#include <U8g2lib.h>

// ============================================================================
// 16-BAND GRAPHIC EQUALIZER - EXTERNAL MODULE
// ============================================================================
// Created for easy integration with new firmware versions
// Simply include this file and call EQ16_init() in setup()
// ============================================================================

#define EQ16_BANDS 16
#define EQ16_GAIN_MIN -12
#define EQ16_GAIN_MAX 12

// 16-band equalizer structure
struct EQ16_Band {
    float frequency;        // Center frequency in Hz
    int8_t gain;           // Gain in dB (-12 to +12)
    const char* name;      // Display name
    
    // IIR Filter coefficients (will be calculated)
    float b0, b1, b2;      // Numerator coefficients
    float a1, a2;          // Denominator coefficients
    
    // Filter state (for processing)
    float x1, x2;          // Input history
    float y1, y2;          // Output history
};

// Main EQ16 class
class EQ16_GraphicEQ {
private:
    static EQ16_Band bands[EQ16_BANDS];
    static bool enabled;
    static bool menuActive;
    static uint8_t selectedBand;
    static bool settingsChanged;
    static unsigned long lastSaveTime;
    
    // Internal methods
    static void calculateFilterCoefficients(uint8_t bandIndex);
    static void resetFilterStates();
    static float processSample(float sample, uint8_t bandIndex);
    
public:
    // Initialization
    static void init();
    static void enable(bool state);
    static bool isEnabled() { return enabled; }
    
    // Menu control
    static void setMenuActive(bool active);
    static bool isMenuActive() { return menuActive; }
    
    // Band selection
    static void selectNextBand();
    static void selectPrevBand();
    static uint8_t getSelectedBand() { return selectedBand; }
    static const char* getBandName(uint8_t band);
    static float getBandFrequency(uint8_t band);
    
    // Gain control
    static void increaseBandGain();
    static void decreaseBandGain();
    static int8_t getBandGain(uint8_t band);
    static void setBandGain(uint8_t band, int8_t gain);
    static int8_t* getAllGains();
    
    // Presets
    static void resetAllBands();
    static void loadPreset(uint8_t presetId);
    
    // Audio processing
    static float processSampleStereo(float leftSample, float rightSample, bool isLeft);
    static void processStereo(int16_t* leftSamples, int16_t* rightSamples, size_t sampleCount);
    
    // Display
    static void displayMenu();
    
    // Storage
    static void saveToSD();
    static void loadFromSD();
    static void autoSave(); // Call periodically
    
    // Diagnostics
    static void printConfiguration();
    static void printBandInfo(uint8_t band);
};

// Global functions for easy integration
extern void EQ16_init();
extern void EQ16_enable(bool state);
extern bool EQ16_isEnabled();
extern void EQ16_displayMenu();
extern void EQ16_setMenuActive(bool active);
extern bool EQ16_isMenuActive();
extern void EQ16_selectNextBand();
extern void EQ16_selectPrevBand();
extern void EQ16_increaseBandGain();
extern void EQ16_decreaseBandGain();
extern void EQ16_resetAllBands();
extern void EQ16_loadPreset(uint8_t presetId);
extern void EQ16_saveToSD();
extern void EQ16_loadFromSD();
extern void EQ16_autoSave();
extern void EQ16_processStereo(int16_t* left, int16_t* right, size_t count);
extern float EQ16_processSample(float leftSample, float rightSample, bool isLeft);

// Integration callbacks - implement these in main.cpp if needed
extern void EQ16_onSettingsChanged();  // Called when settings change
extern void EQ16_onMenuEnter();        // Called when entering menu
extern void EQ16_onMenuExit();         // Called when exiting menu

// External dependencies - must be available in main.cpp
extern U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2;  // Display object

#endif // EQ16_GRAPHICEQ_H