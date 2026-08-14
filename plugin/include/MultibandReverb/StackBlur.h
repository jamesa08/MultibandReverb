#pragma once
// Stack Blur Algorithm by Mario Klingemann <mario@quasimondo.com>
// BSD-3-Clause License
// Source: https://github.com/FigBug/Gin (gin_imageeffects_stackblur.cpp)
// Lifted verbatim; no Gin dependency required.
#include <JuceHeader.h>

void applyStackBlur(juce::Image& img, int radius);
