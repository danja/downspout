#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Downspout / Valis / Transmission — shared visual design tokens
//
// AESTHETIC: Cold War era electronic test equipment (Tektronix, HP, Marconi).
//   Light theme: warm battleship-grey front panel, near-black bezel, amber
//   indicator lamps, stencil-style labels — minimal and brutal but legible.
//   Dark theme: direct inverse — charcoal panel, cream bezel, same amber.
//
// USAGE: Each framework draws using its own API; map tokens to draw calls.
//   DPF/NanoVG:  fillColor(t.panel.r, t.panel.g, t.panel.b, t.panel.a)
//   JUCE:        g.setColour(juce::Colour(t.panel.r, t.panel.g, t.panel.b))
//   A global or per-plugin Theme pointer selects light vs dark at runtime.
//
// EXTENDING: Add new semantic slots here rather than inventing local colours
//   in individual plugins. Keep names role-based (what it IS) not hue-based
//   (what colour it happens to be). Avoid adding slots for one-off uses.
// ─────────────────────────────────────────────────────────────────────────────

namespace downspout::laf {

// ── Colour primitive ──────────────────────────────────────────────────────────

struct Colour {
    uint8_t r, g, b, a;

    constexpr Colour() : r(0), g(0), b(0), a(255) {}
    constexpr Colour(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    // Return a copy with modified opacity.
    [[nodiscard]] constexpr Colour withAlpha(uint8_t a_) const { return {r, g, b, a_}; }

    // Blend toward black by factor 0–255 (0 = unchanged, 255 = black).
    [[nodiscard]] constexpr Colour darker(uint8_t amount) const
    {
        const auto scale = static_cast<uint16_t>(255 - amount);
        return { static_cast<uint8_t>(r * scale / 255),
                 static_cast<uint8_t>(g * scale / 255),
                 static_cast<uint8_t>(b * scale / 255), a };
    }

    // Blend toward white by factor 0–255 (0 = unchanged, 255 = white).
    [[nodiscard]] constexpr Colour lighter(uint8_t amount) const
    {
        const auto inv = static_cast<uint8_t>(255 - amount);
        return { static_cast<uint8_t>(255 - (255 - r) * inv / 255),
                 static_cast<uint8_t>(255 - (255 - g) * inv / 255),
                 static_cast<uint8_t>(255 - (255 - b) * inv / 255), a };
    }
};

// ── Semantic theme ─────────────────────────────────────────────────────────────
//
// Slot naming conventions
// ───────────────────────
//   background  — window / outermost fill behind all panels
//   panel       — primary control surface (the front panel itself)
//   surface     — slightly raised inset area or sub-panel
//   bezel       — outer border/frame that surrounds a section or the whole plugin
//   border      — lighter hairline between grouped controls
//   textPrimary — main label and value text; must contrast strongly on panel
//   textDim     — secondary or unit labels; readable but recessed
//   textDisabled— controls that are currently inactive
//   accent      — primary active/hot colour (amber lamp, lit button)
//   accentDim   — unlit version of accent (indicator housing, off state)
//   meterOn     — active meter bar / lit LED segment
//   meterOff    — unlit meter segment (the track)
//   controlTrack— potentiometer or slider track (the groove)
//   controlFill — filled portion of a control track up to the current value
//   controlKnob — knob or thumb face colour
//   buttonFace  — inactive push-button face
//   buttonHot   — push-button face when pressed or armed
//   selection   — list row highlight, waveform selection overlay
//   danger      — error states, clipping indicators, fault lamps
//   warning     — near-clip or caution state

struct Theme {
    Colour background;
    Colour panel;
    Colour surface;
    Colour bezel;
    Colour border;

    Colour textPrimary;
    Colour textDim;
    Colour textDisabled;

    Colour accent;
    Colour accentDim;

    Colour meterOn;
    Colour meterOff;

    Colour controlTrack;
    Colour controlFill;
    Colour controlKnob;

    Colour buttonFace;
    Colour buttonHot;

    Colour selection;

    Colour danger;
    Colour warning;
};

// ── Light theme — warm grey front panel, amber lamps ─────────────────────────
//
// Reference: HP 8920A, Marconi TF 1370, Tektronix 556.
// Panel surface is warm battleship grey-cream. Bezel and text are near-black.
// Accent and meters use incandescent amber, reminiscent of VFD/lamp indicators.
// Controls are visually heavy: dark tracks, minimal curves, no gradients.

inline constexpr Theme kLightTheme = {
    /* background   */ { 168, 162, 150, 255 },  // darker warm grey surround
    /* panel        */ { 200, 195, 180, 255 },  // battleship grey-cream
    /* surface      */ { 216, 211, 198, 255 },  // slightly raised inset
    /* bezel        */ {  22,  20,  16, 255 },  // near-black frame
    /* border       */ { 140, 135, 122, 255 },  // subtle panel seam

    /* textPrimary  */ {  18,  16,  12, 255 },  // near-black stencil label
    /* textDim      */ {  74,  68,  56, 255 },  // secondary/unit text
    /* textDisabled */ { 140, 134, 120, 255 },  // greyed-out label

    /* accent       */ { 210, 118,  10, 255 },  // amber indicator lamp
    /* accentDim    */ {  68,  38,   6, 255 },  // unlit lamp housing

    /* meterOn      */ { 218, 128,  12, 255 },  // lit amber bar segment
    /* meterOff     */ {  52,  44,  32, 255 },  // unlit segment (dark recess)

    /* controlTrack */ {  36,  32,  24, 255 },  // potentiometer groove
    /* controlFill  */ { 210, 118,  10, 255 },  // filled arc / bar
    /* controlKnob  */ { 190, 186, 174, 255 },  // knob face (lighter grey)

    /* buttonFace   */ { 184, 178, 164, 255 },  // inactive push-button
    /* buttonHot    */ { 210, 118,  10, 255 },  // active / armed button

    /* selection    */ { 210, 118,  10,  60 },  // list/wave selection overlay

    /* danger       */ { 188,  28,  28, 255 },  // fault lamp / clip
    /* warning      */ { 200,  98,  14, 255 },  // near-clip / caution
};

// ── Dark theme — charcoal panel, cream bezel, same amber ─────────────────────
//
// Structural inverse of the light theme. Same amber accent and geometry.
// Background becomes near-black; bezel and text swap to cream.
// Matches the colour language already used in existing Downspout plugins.

inline constexpr Theme kDarkTheme = {
    /* background   */ {  10,  13,  18, 255 },  // near-black surround
    /* panel        */ {  22,  28,  36, 255 },  // dark blue-charcoal
    /* surface      */ {  30,  38,  48, 255 },  // slightly raised inset
    /* bezel        */ { 200, 195, 180, 255 },  // cream frame (light theme panel)
    /* border       */ {  44,  54,  66, 255 },  // subtle panel seam

    /* textPrimary  */ { 228, 224, 212, 255 },  // cream stencil label
    /* textDim      */ { 140, 136, 120, 255 },  // secondary/unit text
    /* textDisabled */ {  72,  70,  62, 255 },  // greyed-out label

    /* accent       */ { 210, 118,  10, 255 },  // same amber (unchanged)
    /* accentDim    */ {  58,  34,   6, 255 },  // unlit lamp housing

    /* meterOn      */ { 218, 128,  12, 255 },  // lit amber bar
    /* meterOff     */ {  36,  30,  20, 255 },  // unlit segment

    /* controlTrack */ {  42,  50,  62, 255 },  // recessed groove on dark panel
    /* controlFill  */ { 210, 118,  10, 255 },  // filled arc / bar
    /* controlKnob  */ {  48,  58,  70, 255 },  // knob face (dark grey)

    /* buttonFace   */ {  36,  44,  56, 255 },  // inactive button
    /* buttonHot    */ { 210, 118,  10, 255 },  // active / armed button

    /* selection    */ { 210, 118,  10,  55 },  // list/wave selection overlay

    /* danger       */ { 210,  44,  44, 255 },  // fault / clip
    /* warning      */ { 210, 110,  20, 255 },  // near-clip / caution
};

// ── Geometry constants ────────────────────────────────────────────────────────
//
// Keep corners tight. Cold War instruments had angular, machined enclosures —
// rounded corners are a post-80s consumer aesthetic. Use kRadiusPanel only
// for the outermost plugin window where the host clips anyway.

inline constexpr float kRadiusNone    = 0.0f;  // flat rectangular — preferred
inline constexpr float kRadiusSmall   = 2.0f;  // group box or button edge
inline constexpr float kRadiusPanel   = 4.0f;  // outermost plugin window only

inline constexpr float kBorderWidth   = 1.0f;  // hairline between sections
inline constexpr float kBezelWidth    = 2.0f;  // outer plugin frame

inline constexpr float kPad           = 6.0f;  // standard internal padding
inline constexpr float kGap           = 4.0f;  // gap between adjacent controls
inline constexpr float kSectionGap    = 10.0f; // gap between labelled groups

// ── Typography scale ─────────────────────────────────────────────────────────
//
// All sizes in pixels for a 96 DPI reference. Scale linearly for HiDPI.
// Prefer all-caps labels with letter-spacing for the stencil-plate look.

inline constexpr float kFontSizeLabel  = 10.0f; // control labels, group headers
inline constexpr float kFontSizeValue  = 11.0f; // displayed parameter values
inline constexpr float kFontSizeSmall  =  9.0f; // units, sub-labels, tooltips
inline constexpr float kFontSizeTitle  = 12.0f; // plugin name / section title

// ── Convenience ───────────────────────────────────────────────────────────────

// Returns the standard theme for new plugins until per-plugin preferences exist.
inline const Theme& defaultTheme() { return kDarkTheme; }

}  // namespace downspout::laf
