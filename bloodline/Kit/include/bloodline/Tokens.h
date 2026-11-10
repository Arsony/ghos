#pragma once
// BloodlineKit - colour tokens (JUCE-free, 0xAARRGGBB). Gen 1 tokens stay;
// the world accents are per the Gen 2 bible. If your existing Bloodline.h
// already defines these, keep that one and delete this file.
#include <cstdint>

namespace bloodline::tokens {
constexpr uint32_t voidBlack  = 0xff070708;
constexpr uint32_t obsidian   = 0xff0d0d10;
constexpr uint32_t panel      = 0xff141418;
constexpr uint32_t silver     = 0xffbfc0c5;
constexpr uint32_t silverHi   = 0xffe6e7ea;
constexpr uint32_t crimson    = 0xff8f1024;
constexpr uint32_t blood      = 0xffc51f38;
constexpr uint32_t glow       = 0xffe12b49;
// Ghost world
constexpr uint32_t moonsilver   = 0xffd9e6ff;
constexpr uint32_t spectralBlue = 0xff8fb4ff;
} // namespace bloodline::tokens
