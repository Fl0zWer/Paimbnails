#pragma once

#include <cstdint>
#include <utility>
#include <cocos2d.h>

struct DCColor { uint8_t r, g, b; };

namespace DominantColors {
    // Extract two dominant colors from an RGB24 buffer. Returns {A,B}.
    // If only one distinct color is found, both will be identical.
    std::pair<DCColor, DCColor> extract(const uint8_t* rgb, int width, int height);
}

