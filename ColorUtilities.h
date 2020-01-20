
#pragma once

#include <Arduino.h>

void HSV2RGB(float h, float s, float v, byte *r, byte *g, byte *b);
void HSV2RGB(float h, float s, float v, byte *rgb);
