#pragma once
// AutoPress Pro: compressor types, stepped dial values and THE Lite/Pro split.
// Everything about what is free and what needs Pro lives in this file. To move a feature,
// flip its flag below: the processor (audio), the editor (padlocks) and the tests all read it.

namespace ap
{
enum Type { clean = 0, glue, punch, smooth, warm, numTypes };

inline const char* typeName (int t)
{
    static const char* n[] = { "CLEAN", "GLUE", "PUNCH", "SMOOTH", "WARM" };
    return n[t >= 0 && t < numTypes ? t : 0];
}

// Stepped dials (SSL style). The last position of attack / release / ratio is AUTO.
inline constexpr float attackStepsMs[]  = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
inline constexpr float releaseStepsMs[] = { 100.0f, 300.0f, 600.0f, 1200.0f };
inline constexpr float ratioSteps[]     = { 1.5f, 2.0f, 4.0f, 10.0f };
inline constexpr float hpfStepsHz[]     = { 0.0f, 60.0f, 90.0f, 150.0f };   // 0 = OFF
inline constexpr int numAttackSteps  = 6, attackAuto  = 6;
inline constexpr int numReleaseSteps = 4, releaseAuto = 4;
inline constexpr int numRatioSteps   = 4, ratioAuto   = 4;
inline constexpr int numHpfSteps     = 4;

// ── THE LITE / PRO SPLIT ──────────────────────────────────────────────────────
// true = needs Pro (licence key or 7-day trial). false = free in Lite.
// Always free: auto-learn + LEARN, THRESHOLD, MIX, auto makeup, meters.
namespace gate
{
    inline constexpr bool proType[numTypes] = {
        false,  // CLEAN  (the original AutoPress sound)
        false,  // GLUE   (free: existing AutoPress users rely on it)
        true,   // PUNCH
        true,   // SMOOTH
        true    // WARM
    };
    inline constexpr bool proAttack  = true;   // Lite: locked to AUTO
    inline constexpr bool proRelease = true;   // Lite: locked to AUTO
    inline constexpr bool proRatio   = true;   // Lite: AUTO (learned ratio, 2:1 in GLUE)
    inline constexpr bool proScHpf   = true;   // Lite: OFF
    inline constexpr bool proDrive   = true;   // Lite: 0 %

    inline bool typeAllowed (int t, bool pro) { return pro || ! proType[t >= 0 && t < numTypes ? t : 0]; }
    // What a locked type falls back to in Lite.
    inline int  effectiveType (int t, bool pro) { return typeAllowed (t, pro) ? t : (int) clean; }
}
} // namespace ap
