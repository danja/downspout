#include "DistrhoUI.hpp"

#include "box_core_types.hpp"
#include "box_params.hpp"
#include "downspout/look_and_feel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;
namespace core = downspout::box;

namespace {

using core::ParamId;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Accent {
    int r;
    int g;
    int b;
};

constexpr Accent kWoodAccent { 198, 132, 58 };
constexpr Accent kAirAccent { 92, 140, 156 };
constexpr Accent kPickupAccent { 124, 148, 104 };
constexpr Accent kRouteAccent { 140, 98, 150 };
constexpr laf::Colour kCream { 250, 248, 242, 255 };

constexpr float kPlotMinHz = 20.0f;
constexpr float kPlotMaxHz = 12000.0f;
constexpr int kPlotSegments = 150;

[[nodiscard]] float clampf(const float value, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(value, hi));
}

[[nodiscard]] std::uint32_t idx(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

[[nodiscard]] float plotXForHz(const Rect& plot, const float hz) noexcept
{
    const float lo = std::log(kPlotMinHz);
    const float hi = std::log(kPlotMaxHz);
    const float t = (std::log(clampf(hz, kPlotMinHz, kPlotMaxHz)) - lo) / (hi - lo);
    return plot.x + t * plot.w;
}

[[nodiscard]] float biquadMagnitude(const core::BiquadCoeffs& c, const float hz,
                                    const double sampleRate) noexcept
{
    const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(hz) / sampleRate;
    const double cw = std::cos(w);
    const double c2 = std::cos(2.0 * w);
    const double sw = std::sin(w);
    const double s2 = std::sin(2.0 * w);

    const double numRe = c.b0 + c.b1 * cw + c.b2 * c2;
    const double numIm = -(c.b1 * sw + c.b2 * s2);
    const double denRe = 1.0 + c.a1 * cw + c.a2 * c2;
    const double denIm = -(c.a1 * sw + c.a2 * s2);
    const double num = numRe * numRe + numIm * numIm;
    const double den = denRe * denRe + denIm * denIm;
    return static_cast<float>(std::sqrt(num / std::max(den, 1.0e-12)));
}

std::string formatValue(const ParamId id, const float value)
{
    char buf[64];
    switch (id) {
    case ParamId::body:
        return core::kBodyNames[clampf(value, 0.0f, 3.0f)];
    case ParamId::size:
        std::snprintf(buf, sizeof(buf), "%.2fx", static_cast<double>(value));
        break;
    case ParamId::thickness:
    case ParamId::hole:
    case ParamId::depth:
        std::snprintf(buf, sizeof(buf), "%.0f mm", static_cast<double>(value));
        break;
    case ParamId::stiffness:
        std::snprintf(buf, sizeof(buf), "%.1f GPa", static_cast<double>(value));
        break;
    case ParamId::density:
        std::snprintf(buf, sizeof(buf), "%.0f kg/m3", static_cast<double>(value));
        break;
    case ParamId::damping:
        std::snprintf(buf, sizeof(buf), "%.2fx", static_cast<double>(value));
        break;
    case ParamId::volume:
        std::snprintf(buf, sizeof(buf), "%.0f L", static_cast<double>(value));
        break;
    case ParamId::drive:
    case ParamId::output:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(value));
        break;
    case ParamId::nonlinear:
    case ParamId::excX:
    case ParamId::excY:
    case ParamId::micX:
    case ParamId::micY:
    case ParamId::width:
    case ParamId::mix:
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(value));
        break;
    case ParamId::ccChannel:
        std::snprintf(buf, sizeof(buf), "Ch %d", static_cast<int>(value));
        break;
    case ParamId::seed:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(value));
        break;
    case ParamId::outBodyHz:
        std::snprintf(buf, sizeof(buf), "%.1f Hz", static_cast<double>(value));
        break;
    case ParamId::outAirHz:
        std::snprintf(buf, sizeof(buf), value > 0.5f ? "%.1f Hz" : "sealed",
                      static_cast<double>(value));
        break;
    case ParamId::outDecay:
        std::snprintf(buf, sizeof(buf), "%.2f s", static_cast<double>(value));
        break;
    case ParamId::outQ:
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(value));
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
        break;
    }
    return buf;
}

}  // namespace

class BoxUI : public UI
{
public:
    BoxUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::size_t i = 0; i < core::kParameterCount; ++i)
            values_[i] = core::kParameterSpecs[i].defaultValue;
        deriveOutputs();

        #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        #else
        loadSharedResources();
        #endif
    }

protected:
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index >= values_.size())
            return;
        values_[index] = value;
        deriveOutputs();
        repaint();
    }

    void onNanoDisplay() override
    {
        hitCount_ = 0;
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());

        drawBackground(w, h);
        drawHeader({20.0f, 16.0f, w - 40.0f, 64.0f});

        drawStructurePanel({20.0f, 92.0f, 300.0f, 360.0f});
        drawCavityPanel({332.0f, 92.0f, 300.0f, 360.0f});
        drawPickupPanel({644.0f, 92.0f, 296.0f, 360.0f});
        drawIntegrationPanel({20.0f, 464.0f, w - 40.0f, 236.0f});
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (!ev.press) {
            activeSlider_ = -1;
            activePad_ = -1;
            return false;
        }

        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        if (themeRect_.contains(mx, my)) {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }
        if (randomiseRect_.contains(mx, my)) {
            triggerRandomise();
            return true;
        }
        if (bypassRect_.contains(mx, my)) {
            commit(ParamId::bypass, values_[idx(ParamId::bypass)] > 0.5f ? 0.0f : 1.0f);
            return true;
        }

        for (std::size_t i = 0; i < hitCount_; ++i) {
            const Hit& hit = hits_[i];
            if (!hit.bounds.contains(mx, my))
                continue;

            if (hit.type == HitType::pad) {
                activePad_ = hit.id == ParamId::excX ? 0 : 1;
                setPad(hit, mx, my);
                return true;
            }
            if (hit.type == HitType::stepper) {
                step(hit.id, mx < hit.bounds.x + hit.bounds.w * 0.5f ? -1 : 1);
                return true;
            }
            if (hit.type == HitType::slider) {
                activeSlider_ = static_cast<int>(idx(hit.id));
                setSlider(hit, mx);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0 && activePad_ < 0)
            return false;

        for (std::size_t i = 0; i < hitCount_; ++i) {
            const Hit& hit = hits_[i];
            if (activeSlider_ >= 0 && hit.type == HitType::slider
                && static_cast<int>(idx(hit.id)) == activeSlider_) {
                setSlider(hit, ev.pos.getX());
                return true;
            }
            if (activePad_ >= 0 && hit.type == HitType::pad
                && ((activePad_ == 0 && hit.id == ParamId::excX)
                    || (activePad_ == 1 && hit.id == ParamId::micX))) {
                setPad(hit, ev.pos.getX(), ev.pos.getY());
                return true;
            }
        }
        return false;
    }

private:
    enum class HitType { slider, stepper, pad };

    struct Hit {
        HitType type = HitType::slider;
        ParamId id = ParamId::body;
        Rect bounds {};
    };

    [[nodiscard]] const laf::Theme& theme() const noexcept
    {
        return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme;
    }

    void setFill(const laf::Colour& c, const int alpha = 255)
    {
        fillColor(c.r, c.g, c.b, static_cast<unsigned char>(alpha));
    }

    void setStroke(const laf::Colour& c, const int alpha = 255)
    {
        strokeColor(c.r, c.g, c.b, static_cast<unsigned char>(alpha));
    }

    void setFill(const Accent& c, const int alpha = 255)
    {
        fillColor(c.r, c.g, c.b, static_cast<unsigned char>(alpha));
    }

    void setStroke(const Accent& c, const int alpha = 255)
    {
        strokeColor(c.r, c.g, c.b, static_cast<unsigned char>(alpha));
    }

    void textAt(const float x, const float y, const char* s, const laf::Colour& c,
                const float size = 10.0f, const int align = ALIGN_LEFT | ALIGN_TOP)
    {
        fontSize(size);
        textAlign(align);
        setFill(c);
        text(x, y, s, nullptr);
    }

    [[nodiscard]] float value(const ParamId id) const noexcept
    {
        return values_[idx(id)];
    }

    void remember(const HitType type, const ParamId id, const Rect bounds)
    {
        if (hitCount_ < hits_.size())
            hits_[hitCount_++] = { type, id, bounds };
    }

    void commit(const ParamId id, const float raw)
    {
        const std::uint32_t i = idx(id);
        const core::ParamSpec& spec = core::kParameterSpecs[i];
        float next = clampf(raw, spec.minimum, spec.maximum);
        if (spec.integer)
            next = std::round(next);
        values_[i] = next;
        deriveOutputs();
        setParameterValue(i, next);
        repaint();
    }

    void commitIfChanged(const ParamId id, const float next)
    {
        if (value(id) == next)
            return;
        commit(id, next);
    }

    void triggerRandomise()
    {
        // HeavyVerb pattern: the action is applied as ordinary parameter values,
        // so the host, processor and panel all end up on the same new body.
        // DPF's VST3 bridge deliberately suppresses kParameterIsTrigger writes,
        // so a trigger parameter cannot do this from the UI.
        const core::Parameters current = currentCore();
        const core::Parameters next = core::randomiseParameters(current);

        commitIfChanged(ParamId::body, next.body);
        commitIfChanged(ParamId::size, next.size);
        commitIfChanged(ParamId::thickness, next.thickness);
        commitIfChanged(ParamId::stiffness, next.stiffness);
        commitIfChanged(ParamId::density, next.density);
        commitIfChanged(ParamId::damping, next.damping);
        commitIfChanged(ParamId::hole, next.hole);
        commitIfChanged(ParamId::volume, next.volume);
        commitIfChanged(ParamId::depth, next.depth);
        commitIfChanged(ParamId::drive, next.drive);
        commitIfChanged(ParamId::nonlinear, next.nonlinear);
        commitIfChanged(ParamId::excX, next.excX);
        commitIfChanged(ParamId::excY, next.excY);
        commitIfChanged(ParamId::micX, next.micX);
        commitIfChanged(ParamId::micY, next.micY);
        commitIfChanged(ParamId::width, next.width);
        commitIfChanged(ParamId::seed, next.seed);

        values_[idx(ParamId::randomise)] += 1.0f;
        repaint();
    }

    void step(const ParamId id, const int direction)
    {
        const core::ParamSpec& spec = core::kParameterSpecs[idx(id)];
        float next = value(id) + static_cast<float>(direction);
        if (next < spec.minimum)
            next = spec.maximum;
        if (next > spec.maximum)
            next = spec.minimum;
        commit(id, next);
    }

    void setSlider(const Hit& hit, const float mouseX)
    {
        const float t = clampf((mouseX - hit.bounds.x) / std::max(1.0f, hit.bounds.w), 0.0f, 1.0f);
        commit(hit.id, core::normalizedToValue(idx(hit.id), t));
    }

    void setPad(const Hit& hit, const float mouseX, const float mouseY)
    {
        const float tx = clampf((mouseX - hit.bounds.x) / std::max(1.0f, hit.bounds.w), 0.0f, 1.0f);
        const float ty = clampf((mouseY - hit.bounds.y) / std::max(1.0f, hit.bounds.h), 0.0f, 1.0f);
        if (hit.id == ParamId::excX) {
            commit(ParamId::excX, tx * 100.0f);
            commit(ParamId::excY, (1.0f - ty) * 100.0f);
        } else {
            commit(ParamId::micX, tx * 100.0f);
            commit(ParamId::micY, (1.0f - ty) * 100.0f);
        }
    }

    [[nodiscard]] core::Parameters currentCore() const noexcept
    {
        core::Parameters p;
        p.body = value(ParamId::body);
        p.size = value(ParamId::size);
        p.thickness = value(ParamId::thickness);
        p.stiffness = value(ParamId::stiffness);
        p.density = value(ParamId::density);
        p.damping = value(ParamId::damping);
        p.hole = value(ParamId::hole);
        p.volume = value(ParamId::volume);
        p.depth = value(ParamId::depth);
        p.drive = value(ParamId::drive);
        p.nonlinear = value(ParamId::nonlinear);
        p.excX = value(ParamId::excX);
        p.excY = value(ParamId::excY);
        p.micX = value(ParamId::micX);
        p.micY = value(ParamId::micY);
        p.width = value(ParamId::width);
        p.mix = value(ParamId::mix);
        p.output = value(ParamId::output);
        p.bypass = value(ParamId::bypass);
        p.ccChannel = value(ParamId::ccChannel);
        p.seed = value(ParamId::seed);
        return core::clampParameters(p);
    }

    void deriveOutputs()
    {
        const core::BodyState body = core::analyseBody(currentCore(), 48000.0);
        values_[idx(ParamId::outBodyHz)] = body.bodyHz;
        values_[idx(ParamId::outAirHz)] = body.airHz;
        values_[idx(ParamId::outDecay)] = body.decaySec;
        values_[idx(ParamId::outQ)] = body.q;
    }

    // ── Drawing primitives ─────────────────────────────────────────────────

    void drawBackground(const float w, const float h)
    {
        const auto& t = theme();
        beginPath();
        setFill(t.background);
        rect(0.0f, 0.0f, w, h);
        fill();
        closePath();
    }

    void drawPanel(const Rect bounds, const char* title, const Accent& accent)
    {
        const auto& t = theme();
        beginPath();
        setFill(t.surface);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusPanel);
        fill();
        closePath();

        beginPath();
        setFill(accent);
        roundedRect(bounds.x, bounds.y, bounds.w, 28.0f, laf::kRadiusPanel);
        fill();
        rect(bounds.x, bounds.y + 18.0f, bounds.w, 10.0f);
        fill();
        closePath();

        textAt(bounds.x + 12.0f, bounds.y + 8.0f, title, kCream, 12.0f,
               ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawSlider(const ParamId id, const char* label, const Rect bounds, const Accent& accent)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const float norm = core::valueToNormalized(parameter, values_[parameter]);

        textAt(bounds.x, bounds.y, label, t.textDim, 11.0f);
        const std::string shown = formatValue(id, values_[parameter]);
        textAt(bounds.x + bounds.w, bounds.y, shown.c_str(), t.textPrimary, 11.0f,
               ALIGN_RIGHT | ALIGN_TOP);

        const Rect track { bounds.x, bounds.y + 18.0f, bounds.w, 10.0f };
        beginPath();
        setFill(t.controlTrack);
        roundedRect(track.x, track.y, track.w, track.h, 3.0f);
        fill();
        closePath();

        if (norm > 0.002f) {
            beginPath();
            setFill(accent);
            roundedRect(track.x, track.y, std::max(2.0f, track.w * norm), track.h, 3.0f);
            fill();
            closePath();
        }
        remember(HitType::slider, id, { track.x, track.y - 7.0f, track.w, track.h + 14.0f });
    }

    void drawStepper(const ParamId id, const char* label, const Rect bounds,
                     const std::string& shown)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y, label, t.textDim, 11.0f);

        const Rect box { bounds.x, bounds.y + 17.0f, bounds.w, 22.0f };
        beginPath();
        setFill(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        closePath();
        beginPath();
        setStroke(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, laf::kRadiusSmall);
        stroke();
        closePath();

        textAt(box.x + box.w * 0.5f, box.y + box.h * 0.5f, shown.c_str(), t.textPrimary,
               11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        textAt(box.x + 7.0f, box.y + box.h * 0.5f, "<", t.textDim, 11.0f,
               ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", t.textDim, 11.0f,
               ALIGN_RIGHT | ALIGN_MIDDLE);
        remember(HitType::stepper, id, box);
    }

    void drawXY(const ParamId xId, const ParamId yId, const char* title, const Rect bounds,
                const Accent& accent)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y - 16.0f, title, t.textDim, 10.0f);

        beginPath();
        setFill(t.panel);
        rect(bounds.x, bounds.y, bounds.w, bounds.h);
        fill();
        closePath();
        beginPath();
        setStroke(t.border);
        strokeWidth(1.0f);
        rect(bounds.x + 0.5f, bounds.y + 0.5f, bounds.w - 1.0f, bounds.h - 1.0f);
        stroke();
        closePath();

        setStroke(t.border, 110);
        strokeWidth(1.0f);
        for (int i = 1; i < 4; ++i) {
            const float fx = bounds.x + bounds.w * static_cast<float>(i) / 4.0f;
            const float fy = bounds.y + bounds.h * static_cast<float>(i) / 4.0f;
            beginPath();
            moveTo(fx, bounds.y);
            lineTo(fx, bounds.y + bounds.h);
            stroke();
            closePath();
            beginPath();
            moveTo(bounds.x, fy);
            lineTo(bounds.x + bounds.w, fy);
            stroke();
            closePath();
        }

        const float px = bounds.x + value(xId) / 100.0f * bounds.w;
        const float py = bounds.y + (1.0f - value(yId) / 100.0f) * bounds.h;
        setStroke(accent, 120);
        strokeWidth(1.0f);
        beginPath();
        moveTo(bounds.x, py);
        lineTo(bounds.x + bounds.w, py);
        moveTo(px, bounds.y);
        lineTo(px, bounds.y + bounds.h);
        stroke();
        closePath();

        beginPath();
        setFill(accent);
        circle(px, py, 5.0f);
        fill();
        closePath();

        char label[48];
        std::snprintf(label, sizeof(label), "X %.0f  Y %.0f", value(xId), value(yId));
        textAt(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h + 6.0f, label, t.textDim,
               10.0f, ALIGN_CENTER | ALIGN_TOP);
        remember(HitType::pad, xId, bounds);
    }

    void drawSwitch(const Rect bounds, const char* label, const bool on,
                    const char* onText, const char* offText)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y + bounds.h * 0.5f, label, t.textDim, 11.0f,
               ALIGN_LEFT | ALIGN_MIDDLE);
        textAt(bounds.x + bounds.w - 54.0f, bounds.y + bounds.h * 0.5f,
               on ? onText : offText, t.textPrimary, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);

        const Rect box { bounds.x + bounds.w - 46.0f, bounds.y + 4.0f, 42.0f, 20.0f };
        beginPath();
        if (on)
            setFill(kWoodAccent);
        else
            setFill(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, 3.0f);
        fill();
        closePath();
        textAt(box.x + box.w * 0.5f, box.y + box.h * 0.5f, on ? "ON" : "OFF", kCream,
               9.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        bypassRect_ = box;
    }

    void drawButton(const Rect bounds, const char* label, const Accent& accent)
    {
        beginPath();
        setFill(accent);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        fill();
        closePath();
        textAt(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f, label, kCream,
               12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        randomiseRect_ = bounds;
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawHeader(const Rect bounds)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y + 2.0f, "BOX", kCream, 28.0f);
        textAt(bounds.x + 78.0f, bounds.y + 8.0f, "wooden body resonance — stereo effect",
               t.textPrimary, 13.0f);
        textAt(bounds.x, bounds.y + 42.0f,
               "Input drives the structure. Output is what the wood radiates. CC 1-4: size, damping, hole, drive.",
               t.textDim, 10.0f);

        themeRect_ = { bounds.x + bounds.w - 76.0f, bounds.y + 10.0f, 76.0f, 26.0f };
        beginPath();
        setFill(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        closePath();
        textAt(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f,
               darkTheme_ ? "DARK" : "LIGHT", t.textPrimary, 10.0f,
               ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawStructurePanel(const Rect bounds)
    {
        drawPanel(bounds, "STRUCTURE & MATERIAL", kWoodAccent);
        drawStepper(ParamId::body, "Body", { bounds.x + 16.0f, bounds.y + 32.0f, 268.0f, 40.0f },
                    formatValue(ParamId::body, value(ParamId::body)));
        drawSlider(ParamId::size, "Body Size", { bounds.x + 16.0f, bounds.y + 74.0f, 268.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::thickness, "Thickness", { bounds.x + 16.0f, bounds.y + 114.0f, 268.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::stiffness, "Stiffness", { bounds.x + 16.0f, bounds.y + 154.0f, 268.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::density, "Density", { bounds.x + 16.0f, bounds.y + 194.0f, 268.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::damping, "Damping", { bounds.x + 16.0f, bounds.y + 234.0f, 268.0f, 36.0f }, kWoodAccent);

        const auto& t = theme();
        char line[64];
        std::snprintf(line, sizeof(line), "Mode Q  %s     Decay  %s",
                      formatValue(ParamId::outQ, value(ParamId::outQ)).c_str(),
                      formatValue(ParamId::outDecay, value(ParamId::outDecay)).c_str());
        textAt(bounds.x + 16.0f, bounds.y + 286.0f, line, t.textDim, 10.0f);
        textAt(bounds.x + 16.0f, bounds.y + 306.0f,
               "Plate modes follow size², thickness and √stiffness.", t.textDisabled, 9.0f);
    }

    void drawCavityPanel(const Rect bounds)
    {
        drawPanel(bounds, "AIR & CAVITY", kAirAccent);
        drawSlider(ParamId::hole, "Soundhole", { bounds.x + 16.0f, bounds.y + 32.0f, 268.0f, 36.0f }, kAirAccent);
        drawSlider(ParamId::volume, "Air Volume", { bounds.x + 16.0f, bounds.y + 72.0f, 268.0f, 36.0f }, kAirAccent);
        drawSlider(ParamId::depth, "Body Depth", { bounds.x + 16.0f, bounds.y + 112.0f, 268.0f, 36.0f }, kAirAccent);

        const Rect plot { bounds.x + 16.0f, bounds.y + 160.0f, 268.0f, 112.0f };
        drawResponsePlot(plot);

        const auto& t = theme();
        char line[80];
        std::snprintf(line, sizeof(line), "Body %s   Air %s",
                      formatValue(ParamId::outBodyHz, value(ParamId::outBodyHz)).c_str(),
                      formatValue(ParamId::outAirHz, value(ParamId::outAirHz)).c_str());
        textAt(bounds.x + 16.0f, bounds.y + 292.0f, line, t.textDim, 10.0f);
        textAt(bounds.x + 16.0f, bounds.y + 312.0f,
               "Relative response: plate modes + air + cavity.", t.textDisabled, 9.0f);
    }

    void drawPickupPanel(const Rect bounds)
    {
        drawPanel(bounds, "EXCITATION & PICKUP", kPickupAccent);
        const float padW = 118.0f;
        drawXY(ParamId::excX, ParamId::excY, "EXCITATION",
               { bounds.x + 18.0f, bounds.y + 54.0f, padW, 104.0f }, kWoodAccent);
        drawXY(ParamId::micX, ParamId::micY, "PICKUP",
               { bounds.x + 160.0f, bounds.y + 54.0f, padW, 104.0f }, kPickupAccent);

        drawSlider(ParamId::drive, "Drive", { bounds.x + 18.0f, bounds.y + 190.0f, 260.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::nonlinear, "Wood Nonlinearity", { bounds.x + 18.0f, bounds.y + 230.0f, 260.0f, 36.0f }, kWoodAccent);
        drawSlider(ParamId::width, "Stereo Width", { bounds.x + 18.0f, bounds.y + 270.0f, 260.0f, 36.0f }, kPickupAccent);

        textAt(bounds.x + 18.0f, bounds.y + 318.0f,
               "Pads set sin(mπx)·sin(nπy) mode weights.", theme().textDisabled, 9.0f);
    }

    void drawIntegrationPanel(const Rect bounds)
    {
        drawPanel(bounds, "INTEGRATION & MIDI", kRouteAccent);
        const auto& t = theme();

        drawSlider(ParamId::mix, "Dry / Wood Mix", { bounds.x + 20.0f, bounds.y + 32.0f, 276.0f, 36.0f }, kRouteAccent);
        drawSlider(ParamId::output, "Output", { bounds.x + 20.0f, bounds.y + 72.0f, 276.0f, 36.0f }, kRouteAccent);
        drawSwitch({ bounds.x + 20.0f, bounds.y + 112.0f, 276.0f, 28.0f }, "Bypass",
                   value(ParamId::bypass) > 0.5f, "bypassed", "active");
        drawStepper(ParamId::seed, "Seed", { bounds.x + 20.0f, bounds.y + 146.0f, 132.0f, 40.0f },
                    formatValue(ParamId::seed, value(ParamId::seed)));
        drawStepper(ParamId::ccChannel, "CC Channel", { bounds.x + 164.0f, bounds.y + 146.0f, 132.0f, 40.0f },
                    formatValue(ParamId::ccChannel, value(ParamId::ccChannel)));

        drawButton({ bounds.x + 320.0f, bounds.y + 32.0f, 180.0f, 30.0f }, "RANDOMISE BODY", kRouteAccent);
        drawCcMap({ bounds.x + 320.0f, bounds.y + 84.0f, 276.0f, 120.0f });

        drawSignalFlow({ bounds.x + 620.0f, bounds.y + 32.0f, 300.0f, 64.0f });
        const float tx = bounds.x + 620.0f;
        const float ty = bounds.y + 112.0f;
        textAt(tx, ty, "The input excites a wooden plate; the output is what", t.textDim, 10.0f);
        textAt(tx, ty + 14.0f, "the structure radiates, not the source itself.", t.textDim, 10.0f);
        textAt(tx, ty + 32.0f, "Size, damping, soundhole and drive respond to CC.", t.textDisabled, 9.0f);
        textAt(tx, ty + 46.0f, "Extreme sizes and drive are intentional.", t.textDisabled, 9.0f);
        textAt(tx, ty + 60.0f, "Piano and Free Plate remove the air path.", t.textDisabled, 9.0f);
    }

    void drawCcMap(const Rect bounds)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y - 14.0f, "CC ROUTING (channel from left)", t.textDim, 10.0f);

        beginPath();
        setFill(t.panel);
        rect(bounds.x, bounds.y, bounds.w, bounds.h);
        fill();
        closePath();
        beginPath();
        setStroke(t.border);
        strokeWidth(1.0f);
        rect(bounds.x + 0.5f, bounds.y + 0.5f, bounds.w - 1.0f, bounds.h - 1.0f);
        stroke();
        closePath();

        constexpr const char* kNames[] = { "Body Size", "Damping", "Soundhole", "Drive" };
        for (int i = 0; i < 4; ++i) {
            const float y = bounds.y + 12.0f + static_cast<float>(i) * 26.0f;
            beginPath();
            setFill(kRouteAccent);
            roundedRect(bounds.x + 12.0f, y, 22.0f, 18.0f, 2.0f);
            fill();
            closePath();
            textAt(bounds.x + 23.0f, y + 9.0f, std::to_string(i + 1).c_str(), kCream, 10.0f,
                   ALIGN_CENTER | ALIGN_MIDDLE);
            textAt(bounds.x + 46.0f, y + 9.0f, kNames[i], t.textPrimary, 11.0f,
                   ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    void drawSignalFlow(const Rect bounds)
    {
        const auto& t = theme();
        textAt(bounds.x, bounds.y - 14.0f, "SIGNAL FLOW", t.textDim, 10.0f);
        constexpr const char* kSteps[] = { "IN", "EXCITE", "WOOD", "PICKUP", "OUT" };
        const float boxW = 48.0f;
        const float gap = 13.0f;
        const float y = bounds.y + 14.0f;
        for (int i = 0; i < 5; ++i) {
            const float x = bounds.x + static_cast<float>(i) * (boxW + gap);
            beginPath();
            if (i == 2)
                setFill(kWoodAccent);
            else
                setFill(t.buttonFace);
            rect(x, y, boxW, 30.0f);
            fill();
            closePath();
            textAt(x + boxW * 0.5f, y + 15.0f, kSteps[i], i == 2 ? kCream : t.textPrimary,
                   9.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            if (i < 4)
                textAt(x + boxW + 2.0f, y + 15.0f, ">", t.textDim, 10.0f,
                       ALIGN_LEFT | ALIGN_MIDDLE);
        }
    }

    void drawResponsePlot(const Rect plot)
    {
        const auto& t = theme();
        const core::Parameters params = currentCore();
        const core::BodyState body = core::analyseBody(params, 48000.0);

        textAt(plot.x, plot.y - 13.0f, "MODAL RESPONSE (RELATIVE)", t.textDim, 9.0f);
        beginPath();
        setFill(t.panel);
        rect(plot.x, plot.y, plot.w, plot.h);
        fill();
        closePath();
        beginPath();
        setStroke(t.border);
        strokeWidth(1.0f);
        rect(plot.x + 0.5f, plot.y + 0.5f, plot.w - 1.0f, plot.h - 1.0f);
        stroke();
        closePath();

        constexpr float kTopDb = 6.0f;
        constexpr float kFloorDb = -36.0f;
        const auto yForDb = [&](const float db) {
            return plot.y + clampf((kTopDb - db) / (kTopDb - kFloorDb), 0.0f, 1.0f) * plot.h;
        };

        std::array<float, kPlotSegments + 1> dbValues {};
        float peak = 1.0e-6f;
        for (int i = 0; i <= kPlotSegments; ++i) {
            const float fraction = static_cast<float>(i) / static_cast<float>(kPlotSegments);
            const float hz = std::exp(std::log(kPlotMinHz)
                                      + fraction * (std::log(kPlotMaxHz) - std::log(kPlotMinHz)));
            float mag = body.directGain * 0.25f;
            for (std::size_t ch = 0; ch < core::kAudioChannels; ++ch) {
                for (std::size_t k = 0; k < core::kModeCount; ++k) {
                    const core::ModeResponse& mode = body.modes[ch][k];
                    mag += std::fabs(mode.gain)
                         * biquadMagnitude(mode.filter, hz, 48000.0);
                }
                const core::ModeResponse* extras[] = {
                    &body.air[ch], &body.lowCavity[ch], &body.highCavity[ch],
                };
                for (const core::ModeResponse* extra : extras)
                    mag += std::fabs(extra->gain)
                         * biquadMagnitude(extra->filter, hz, 48000.0);
            }
            peak = std::max(peak, mag);
            dbValues[static_cast<std::size_t>(i)] = 20.0f * std::log10(std::max(mag, 1.0e-6f));
        }
        const float peakDb = 20.0f * std::log10(peak);

        beginPath();
        for (int i = 0; i <= kPlotSegments; ++i) {
            const float x = plot.x + static_cast<float>(i) / static_cast<float>(kPlotSegments) * plot.w;
            const float y = yForDb(dbValues[static_cast<std::size_t>(i)] - peakDb);
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        setStroke(kWoodAccent);
        strokeWidth(1.8f);
        stroke();
        closePath();

        beginPath();
        setStroke(t.border, 120);
        strokeWidth(1.0f);
        const float zeroY = yForDb(-peakDb);
        moveTo(plot.x, zeroY);
        lineTo(plot.x + plot.w, zeroY);
        stroke();
        closePath();

        const auto marker = [&](const float hz, const Accent& accent, const char* label) {
            if (hz < kPlotMinHz || hz > kPlotMaxHz)
                return;
            const float x = plotXForHz(plot, hz);
            beginPath();
            setStroke(accent, 170);
            strokeWidth(1.0f);
            moveTo(x, plot.y);
            lineTo(x, plot.y + plot.h);
            stroke();
            closePath();
            textAt(x + 3.0f, plot.y + 3.0f, label, t.textDim, 8.0f);
        };
        marker(body.bodyHz, kWoodAccent, "top");
        if (body.airHz > 0.0f)
            marker(body.airHz, kAirAccent, "air");

        fontSize(8.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        setFill(t.textDim);
        for (float hz = 100.0f; hz <= 10000.0f; hz *= 10.0f) {
            char buf[12];
            std::snprintf(buf, sizeof(buf), hz >= 1000.0f ? "%gk" : "%g",
                          static_cast<double>(hz) / (hz >= 1000.0f ? 1000.0 : 1.0));
            text(plotXForHz(plot, hz), plot.y + plot.h + 2.0f, buf, nullptr);
        }
    }

    std::array<float, core::kParameterCount> values_ {};
    std::array<Hit, 40> hits_ {};
    std::size_t hitCount_ = 0;
    Rect themeRect_ {};
    Rect randomiseRect_ {};
    Rect bypassRect_ {};
    int activeSlider_ = -1;
    int activePad_ = -1;
    bool darkTheme_ = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoxUI)
};

UI* createUI()
{
    return new BoxUI();
}

END_NAMESPACE_DISTRHO
