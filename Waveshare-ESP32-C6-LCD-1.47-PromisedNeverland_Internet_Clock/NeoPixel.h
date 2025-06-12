#pragma once
#include "Arduino.h"

#define PIN_NEOPIXEL 8
#define DEFAULT_BRIGHTNESS 255  // Default brightness level (0-255)

void Set_Color(uint8_t Red, uint8_t Green, uint8_t Blue);           // Set RGB bead color
void NeoPixel_Loop(uint16_t Waiting);                               // The beads change color in cycles
void Set_Brightness(uint8_t brightness);                            // Set brightness level (0-255)
void NeoPixel_Init(uint8_t initialBrightness = DEFAULT_BRIGHTNESS); // Initialize NeoPixel with default brightness