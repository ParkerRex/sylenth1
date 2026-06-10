#include "PluginEditor.h"

#include "../presets/PresetManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace
{
// ---- Palette (flat midnight graphite with electric accents, Serum/Massive-era) ----
// Names are kept stable so the whole editor re-skins by repointing these values.
const auto background   = juce::Colour::fromRGB(19, 21, 26);    // near-black blue-grey body
const auto headerBg     = juce::Colour::fromRGB(13, 15, 19);    // darker rail (top/footer)
const auto panelBg      = juce::Colour::fromRGB(29, 32, 39);    // flat module card
const auto panelHeader  = juce::Colour::fromRGB(36, 40, 49);    // module caption strip
const auto fieldBg      = juce::Colour::fromRGB(14, 16, 20);    // recessed readout / field
const auto stroke       = juce::Colour::fromRGB(58, 64, 78);    // hairline border highlight
const auto strokeSoft   = juce::Colour::fromRGB(40, 45, 55);    // soft edge / divider
const auto text         = juce::Colour::fromRGB(232, 237, 245); // primary label text
const auto mutedText    = juce::Colour::fromRGB(134, 143, 160); // secondary text
const auto accent       = juce::Colour::fromRGB(54, 213, 226);  // electric cyan (arcs / selection)
const auto live         = juce::Colour::fromRGB(98, 226, 142);  // green (on / meter low)
const auto staged       = juce::Colour::fromRGB(240, 172, 72);  // amber (edited / meter mid)
const auto warn         = juce::Colour::fromRGB(240, 94, 90);   // red (clip / danger)
const auto info         = juce::Colour::fromRGB(118, 158, 240); // blue accents
const auto knobFill     = juce::Colour::fromRGB(38, 42, 51);    // matte knob cap
const auto knobStroke   = juce::Colour::fromRGB(62, 69, 84);    // thin knob rim

// Dark glass display: the preset/diagnostics hub and live scope screen.
const auto lcdBg        = juce::Colour::fromRGB(11, 22, 30);
const auto lcdBgEdge    = juce::Colour::fromRGB(6, 12, 18);
const auto lcdText      = juce::Colour::fromRGB(92, 230, 242);
const auto lcdDim       = juce::Colour::fromRGB(64, 142, 162);
const auto lcdStroke    = juce::Colour::fromRGB(34, 74, 92);

// Shared chrome fills so buttons/tabs restyle from one place.
const auto buttonBg     = juce::Colour::fromRGB(43, 48, 58);    // neutral button fill
const auto buttonRaised = juce::Colour::fromRGB(52, 58, 72);    // emphasized button fill
const auto onAccentText = juce::Colour::fromRGB(10, 14, 18);    // dark text on lit accents

// Functional zone hues colour-code the rack the way Massive groups its sections:
// source cyan, shaping amber, performance blue, modulation magenta, utility slate.
const auto zoneSource   = accent;                               // oscillators / tone
const auto zoneShape    = staged;                               // filter / envelopes / LFO
const auto zonePerform  = info;                                 // voice / amp / ramp / macros
const auto zoneMod      = juce::Colour::fromRGB(224, 108, 218); // modulation routes
const auto zoneUtility  = juce::Colour::fromRGB(106, 116, 132); // browser / MIDI

constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float rotaryEnd   = juce::MathConstants<float>::pi * 2.75f;

// Fixed shell chrome heights (editor body math reads these by name).
constexpr int headerHeight   = 60;
constexpr int layerBarHeight  = 80;
constexpr int tabBarHeight    = 36;
constexpr int footerHeight    = 26;

const juce::Identifier readoutParameterIdProperty { "synthiaReadoutParameterId" };

juce::Font uiFont(float size = 13.0f, bool bold = false)
{
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

// Monospaced face for LCD-style readouts so preset/program/diagnostic text reads like a
// backlit hardware screen (Sylenth's blue display) rather than a desktop label.
juce::Font lcdFont(float size = 13.0f, bool bold = false)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                        bold ? juce::Font::bold : juce::Font::plain));
}

juce::String formatValue(const synth::ParameterSpec& spec, double value)
{
    if (!std::isfinite(value))
        value = 0.0;

    auto snapToDisplayedZero = [](double number, double displayedStep) {
        return std::abs(number) < displayedStep * 0.5 ? 0.0 : number;
    };

    const auto& unit = spec.unit;
    if (unit == "dB")
        return juce::String(snapToDisplayedZero(value, 0.1), 1) + " dB";
    if (unit == "Hz")
    {
        const auto decimals = value < 10.0 ? 2 : 1;
        return juce::String(snapToDisplayedZero(value, decimals == 2 ? 0.01 : 0.1), decimals) + " Hz";
    }
    if (unit == "milliseconds")
    {
        value = snapToDisplayedZero(value, value < 10.0 ? 0.1 : 1.0);
        return value >= 1000.0 ? juce::String(value / 1000.0, 2) + " s"
                               : juce::String(value, value < 10.0 ? 1 : 0) + " ms";
    }
    if (unit == "semitones")
        return juce::String(snapToDisplayedZero(value, 0.1), 1) + " st";
    if (unit == "cents")
        return juce::String(juce::roundToInt(snapToDisplayedZero(value, 1.0))) + " ct";
    if (unit == "percent")
        return juce::String(juce::roundToInt(snapToDisplayedZero(value, 0.01) * 100.0)) + " %";
    if (unit == "degrees")
        return juce::String(juce::roundToInt(snapToDisplayedZero(value, 1.0))) + juce::String::fromUTF8("\xc2\xb0");
    if (unit == "octaves")
    {
        const auto octaves = static_cast<int>(std::round(snapToDisplayedZero(value, 1.0)));
        return (octaves > 0 ? "+" : "") + juce::String(octaves);
    }
    if (unit == "voices")
        return juce::String(static_cast<int>(std::round(snapToDisplayedZero(value, 1.0))));
    return juce::String(snapToDisplayedZero(value, 0.01), 2);
}

void tagSliderReadout(juce::Slider& slider, const std::string& parameterId, const juce::String& displayName)
{
    slider.setName(displayName);
    slider.getProperties().set(readoutParameterIdProperty, juce::String(parameterId));
}

juce::String formatGridValue(const synth::ParameterSpec& spec, double value)
{
    if (!std::isfinite(value))
        value = 0.0;

    if (spec.unit == "normalized" || spec.unit == "percent")
        return juce::String(juce::roundToInt(value * 100.0)) + "%";

    if (spec.unit == "semitones")
    {
        const auto semitones = juce::roundToInt(value);
        return (semitones > 0 ? "+" : "") + juce::String(semitones);
    }

    if (spec.unit == "voices" || spec.unit == "steps" || spec.unit == "octaves")
        return juce::String(juce::roundToInt(value));

    return formatValue(spec, value);
}

double parseValueText(const synth::ParameterSpec& spec, const juce::String& textValue)
{
    const auto trimmedText = textValue.trim().toLowerCase();
    const auto number = trimmedText.retainCharacters("-0123456789.").getDoubleValue();

    if (spec.unit == "percent" || spec.unit == "normalized")
        return trimmedText.containsChar('%') || std::abs(number) > 1.0 ? number / 100.0 : number;

    if (spec.unit == "milliseconds")
    {
        if (trimmedText.contains("ms"))
            return number;
        if (trimmedText.containsChar('s'))
            return number * 1000.0;
    }

    return number;
}

void styleFlatButton(juce::Button& button, juce::Colour fill, juce::Colour textColour = text)
{
    button.setColour(juce::TextButton::buttonColourId, fill);
    button.setColour(juce::TextButton::buttonOnColourId, accent.darker(0.1f));
    button.setColour(juce::TextButton::textColourOffId, textColour);
    button.setColour(juce::TextButton::textColourOnId, onAccentText);
}

void styleChipCaption(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, mutedText);
    label.setFont(uiFont(10.5f));
}

void styleChipValue(juce::Label& label)
{
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, text);
    label.setFont(uiFont(15.0f, true));
}

void styleTextEditor(juce::TextEditor& editor)
{
    editor.setFont(uiFont(12.0f));
    editor.setSelectAllWhenFocused(true);
    editor.setColour(juce::TextEditor::backgroundColourId, fieldBg);
    editor.setColour(juce::TextEditor::textColourId, text);
    editor.setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.35f));
    editor.setColour(juce::TextEditor::highlightedTextColourId, text);
    editor.setColour(juce::TextEditor::outlineColourId, stroke);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, accent);
}

// Draws the small rounded zone tick at the left of a module header so every panel reads
// as a labelled module in a colour-coded rack (Massive-style section identity).
// Dimmed when the module's power toggle is off.
void paintModuleHeaderTick(juce::Graphics& g, juce::Rectangle<int> headerArea,
                           juce::Colour colour, bool enabled = true)
{
    const auto tick = juce::Rectangle<float>(static_cast<float>(headerArea.getX()) + 6.0f,
                                             static_cast<float>(headerArea.getY()) + 7.0f,
                                             3.0f,
                                             static_cast<float>(headerArea.getHeight()) - 14.0f);
    g.setColour(enabled ? colour : colour.withAlpha(0.30f));
    g.fillRoundedRectangle(tick, 1.5f);
}

// Flat module caption strip with a hairline base divider, squared to meet the body.
void paintCaptionBar(juce::Graphics& g, juce::Rectangle<int> header, bool enabled = true)
{
    const auto base = enabled ? panelHeader : panelHeader.darker(0.18f);
    g.setColour(base);
    g.fillRoundedRectangle(header.toFloat().reduced(0.5f), 5.0f);
    g.fillRect(header.withTop(header.getBottom() - 6));
    g.setColour(strokeSoft);
    g.fillRect(header.getX() + 1, header.getBottom() - 1, header.getWidth() - 2, 1);
}

// Flat module card with a single hairline border. Shared by every titled module.
void paintPanelBody(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(panelBg);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(strokeSoft);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

constexpr const char* modSourceDragPrefix = "modsource:";
} // namespace

// ============================================================================
// ModTargetSlider: a knob that is also a modulation drop target.
//
// Knobs whose parameter is a TransMod destination accept mod-source chips
// dragged from the source strip; a magenta halo arc around the knob shows each
// route's modulated range (the Serum-style ring). Alt-drag the knob to adjust
// the first route's depth, right-click to list or clear routes. Routes are
// real transmod.* state via the editor-wired callbacks — nothing local.
// ============================================================================
class SynthAudioProcessorEditor::ModTargetSlider final : public juce::Slider,
                                                         public juce::DragAndDropTarget
{
public:
    struct RouteDisplay
    {
        int slotNumber = 0;
        float depth = 0.0f;
        bool bipolarSource = false;
        bool lfoSource = false;
        juce::String label;

        bool operator==(const RouteDisplay& other) const noexcept
        {
            return slotNumber == other.slotNumber
                && std::abs(depth - other.depth) < 0.0001f
                && bipolarSource == other.bipolarSource
                && lfoSource == other.lfoSource
                && label == other.label;
        }
    };

    std::function<void(const juce::String& sourceId)> onModSourceDropped;
    std::function<void(int slotNumber, float depth)> onDepthEdited;
    std::function<void(int slotNumber)> onSlotCleared;

    void setDestinationInfo(const synth::ModulationDestinationInfo* destinationToUse) noexcept
    {
        destination = destinationToUse;
    }
    bool isModTarget() const noexcept { return destination != nullptr; }
    const synth::ModulationDestinationInfo* destinationInfo() const noexcept { return destination; }

    void setRouteDisplays(std::vector<RouteDisplay> newRoutes)
    {
        if (newRoutes == routes)
            return;
        routes = std::move(newRoutes);
        repaint();
    }

    // Live LFO value from the processor feed animates the halo dot for
    // LFO-sourced routes; cleared when no voice is sounding.
    void setLiveLfo(bool active, float value)
    {
        const auto changed = active != liveLfoActive
            || (active && std::abs(value - liveLfoValue) > 0.005f);
        liveLfoActive = active;
        liveLfoValue = value;
        if (changed && hasLfoRoute())
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        juce::Slider::paint(g);
        if (!isModTarget())
            return;

        const auto layout = getLookAndFeel().getSliderLayout(*this);
        const auto bounds = layout.sliderBounds.toFloat().reduced(3.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centreX = bounds.getCentreX();
        const auto centreY = bounds.getCentreY();
        const auto rotary = getRotaryParameters();

        const auto angleFor = [this, &rotary](double value) {
            const auto clamped = juce::jlimit(getMinimum(), getMaximum(), value);
            const auto proportion = static_cast<float>(valueToProportionOfLength(clamped));
            return rotary.startAngleRadians
                + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
        };

        if (dragHover)
        {
            g.setColour(zoneMod.withAlpha(0.18f));
            g.fillEllipse(bounds.withSizeKeepingCentre(radius * 2.0f, radius * 2.0f));
        }

        const auto value = getValue();
        auto haloRadius = radius + 1.0f;
        for (const auto& route : routes)
        {
            const auto magnitude = std::abs(route.depth);
            const auto lower = route.bipolarSource ? value - magnitude : juce::jmin(value, value + route.depth);
            const auto upper = route.bipolarSource ? value + magnitude : juce::jmax(value, value + route.depth);
            juce::Path halo;
            halo.addCentredArc(centreX, centreY, haloRadius, haloRadius, 0.0f,
                               angleFor(lower), angleFor(upper), true);
            g.setColour(zoneMod.withAlpha(0.85f));
            g.strokePath(halo, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

            if (route.lfoSource && liveLfoActive)
            {
                const auto angle = angleFor(value + static_cast<double>(liveLfoValue) * route.depth);
                const auto dotX = centreX + haloRadius * std::sin(angle);
                const auto dotY = centreY - haloRadius * std::cos(angle);
                g.setColour(text);
                g.fillEllipse(dotX - 2.2f, dotY - 2.2f, 4.4f, 4.4f);
            }

            haloRadius -= 3.0f;
        }

        if (dragHover)
        {
            g.setColour(zoneMod);
            g.drawEllipse(bounds.withSizeKeepingCentre(radius * 2.0f + 3.0f, radius * 2.0f + 3.0f), 1.6f);
        }
    }

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        return isModTarget() && details.description.toString().startsWith(modSourceDragPrefix);
    }

    void itemDragEnter(const SourceDetails&) override
    {
        dragHover = true;
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        dragHover = false;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        dragHover = false;
        repaint();
        const auto sourceId = details.description.toString()
                                  .fromFirstOccurrenceOf(modSourceDragPrefix, false, false);
        if (onModSourceDropped != nullptr && sourceId.isNotEmpty())
            onModSourceDropped(sourceId);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (isModTarget() && !routes.empty() && event.mods.isPopupMenu())
        {
            showRouteMenu();
            return;
        }

        if (isModTarget() && !routes.empty() && event.mods.isAltDown())
        {
            depthDragActive = true;
            depthDragStart = routes.front().depth;
            return;
        }

        juce::Slider::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (depthDragActive)
        {
            if (destination != nullptr && onDepthEdited != nullptr)
            {
                const auto range = destination->maximumDepth - destination->minimumDepth;
                auto depth = depthDragStart
                    - static_cast<float>(event.getDistanceFromDragStartY()) * range / 360.0f;
                depth = juce::jlimit(destination->minimumDepth, destination->maximumDepth, depth);
                if (std::abs(depth) < 0.001f)
                    depth = depth < 0.0f ? -0.001f : 0.001f; // write adapter rejects zero depth
                onDepthEdited(routes.front().slotNumber, depth);
            }
            return;
        }

        juce::Slider::mouseDrag(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (depthDragActive)
        {
            depthDragActive = false;
            return;
        }

        juce::Slider::mouseUp(event);
    }

private:
    bool hasLfoRoute() const noexcept
    {
        return std::any_of(routes.begin(), routes.end(),
                           [](const RouteDisplay& route) { return route.lfoSource; });
    }

    void showRouteMenu()
    {
        juce::PopupMenu menu;
        for (std::size_t i = 0; i < routes.size(); ++i)
        {
            const auto& route = routes[i];
            menu.addItem(static_cast<int>(i) + 1,
                         "Clear slot " + juce::String(route.slotNumber) + ":  " + route.label);
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [this](int result) {
                               const auto index = static_cast<std::size_t>(result - 1);
                               if (result > 0 && index < routes.size() && onSlotCleared != nullptr)
                                   onSlotCleared(routes[index].slotNumber);
                           });
    }

    const synth::ModulationDestinationInfo* destination = nullptr;
    std::vector<RouteDisplay> routes;
    bool dragHover = false;
    bool depthDragActive = false;
    float depthDragStart = 0.0f;
    bool liveLfoActive = false;
    float liveLfoValue = 0.0f;
};

// ============================================================================
// Look and feel: modern rotary knobs, switch toggles, flat combos and buttons.
// ============================================================================
class SynthAudioProcessorEditor::SynthLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SynthLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, accent);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        setColour(juce::ComboBox::backgroundColourId, fieldBg);
        setColour(juce::ComboBox::outlineColourId, stroke);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, accent);

        setColour(juce::PopupMenu::backgroundColourId, panelHeader);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.darker(0.2f));
        setColour(juce::PopupMenu::highlightedTextColourId, onAccentText);

        setColour(juce::TextEditor::backgroundColourId, fieldBg);
        setColour(juce::TextEditor::outlineColourId, stroke);
        setColour(juce::TextEditor::focusedOutlineColourId, accent);
        setColour(juce::TextEditor::textColourId, text);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centreX = bounds.getCentreX();
        const auto centreY = bounds.getCentreY();
        const auto toAngle = startAngle + sliderPos * (endAngle - startAngle);

        // Modern flat knob: a matte cap inside a lit value arc. The arc colour is
        // per-control (rotarySliderFillColourId) so each zone can tint its knobs;
        // bipolar ranges anchor the arc at their zero point instead of the minimum.
        const auto arcColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const auto lit = slider.isEnabled();
        const auto hot = slider.isMouseOverOrDragging() && lit;
        const auto arcRadius = radius - 1.5f;
        const auto arcWidth = juce::jmax(2.0f, radius * 0.14f);

        juce::Path track;
        track.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(strokeSoft.withAlpha(lit ? 0.9f : 0.5f));
        g.strokePath(track, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        auto anchorPos = 0.0f;
        const auto minimum = slider.getMinimum();
        const auto maximum = slider.getMaximum();
        if (minimum < 0.0 && maximum > 0.0)
            anchorPos = static_cast<float>(-minimum / (maximum - minimum));
        const auto anchorAngle = startAngle + anchorPos * (endAngle - startAngle);

        if (std::abs(toAngle - anchorAngle) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc(centreX, centreY, arcRadius, arcRadius, 0.0f,
                              juce::jmin(anchorAngle, toAngle), juce::jmax(anchorAngle, toAngle), true);
            g.setColour(lit ? (hot ? arcColour.brighter(0.25f) : arcColour) : mutedText.withAlpha(0.4f));
            g.strokePath(arc, juce::PathStrokeType(arcWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        // Matte cap with a hairline rim and a soft top sheen.
        const auto capRadius = arcRadius - arcWidth - 1.5f;
        const auto capBox = juce::Rectangle<float>(centreX - capRadius, centreY - capRadius,
                                                   capRadius * 2.0f, capRadius * 2.0f);
        g.setColour(lit ? knobFill : knobFill.darker(0.2f));
        g.fillEllipse(capBox);
        g.setColour(knobStroke.withAlpha(lit ? 1.0f : 0.6f));
        g.drawEllipse(capBox, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillEllipse(capBox.reduced(capRadius * 0.18f).withTrimmedBottom(capRadius * 1.1f));

        // Value pointer.
        const juce::Point<float> tip(centreX + (capRadius - 2.0f) * std::sin(toAngle),
                                     centreY - (capRadius - 2.0f) * std::cos(toAngle));
        const juce::Point<float> root(centreX + (capRadius * 0.35f) * std::sin(toAngle),
                                      centreY - (capRadius * 0.35f) * std::cos(toAngle));
        g.setColour(lit ? text : mutedText);
        g.drawLine({ root, tip }, juce::jmax(1.8f, radius * 0.10f));
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const auto on = button.getToggleState();
        const auto boxWidth = juce::jmin(bounds.getWidth(), 40.0f);
        const auto boxHeight = juce::jmin(bounds.getHeight(), 18.0f);
        auto box = juce::Rectangle<float>(boxWidth, boxHeight).withCentre(bounds.getCentre());
        const auto corner = box.getHeight() * 0.5f;

        // Pill switch: lit accent track with a sliding thumb, hairline when off.
        g.setColour(on ? accent.withAlpha(0.85f) : fieldBg);
        g.fillRoundedRectangle(box, corner);
        g.setColour(on ? accent.brighter(0.1f)
                       : (shouldDrawButtonAsHighlighted ? mutedText : stroke.withAlpha(0.8f)));
        g.drawRoundedRectangle(box.reduced(0.5f), corner, 1.1f);

        const auto thumbDiameter = box.getHeight() - 5.0f;
        const auto thumbX = on ? box.getRight() - thumbDiameter - 2.5f : box.getX() + 2.5f;
        g.setColour(on ? onAccentText : mutedText);
        g.fillEllipse(thumbX, box.getCentreY() - thumbDiameter * 0.5f, thumbDiameter, thumbDiameter);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(fieldBg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(stroke);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        juce::Path arrow;
        const auto arrowArea = juce::Rectangle<float>(static_cast<float>(width) - 18.0f, 0.0f, 14.0f,
                                                      static_cast<float>(height)).reduced(0.0f, static_cast<float>(height) * 0.42f);
        arrow.startNewSubPath(arrowArea.getX(), arrowArea.getY());
        arrow.lineTo(arrowArea.getCentreX(), arrowArea.getBottom());
        arrow.lineTo(arrowArea.getRight(), arrowArea.getY());
        g.setColour(accent);
        g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont(uiFont(12.5f));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(12.5f); }
    juce::Font getPopupMenuFont() override { return uiFont(13.0f); }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = backgroundColour;
        if (shouldDrawButtonAsDown)
            fill = fill.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.08f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(stroke.withAlpha(0.7f));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return uiFont(13.0f, true); }
};

// ============================================================================
// LayoutSection: common contract for fixed-height editor sections.
// ============================================================================
class SynthAudioProcessorEditor::LayoutSection : public juce::Component
{
public:
    virtual int preferredHeight(int width) const = 0;
};

// ============================================================================
// ParameterControl: one APVTS-bound control inside a fixed cell.
// ============================================================================
class SynthAudioProcessorEditor::ParameterControl final : public juce::Component
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const synth::ParameterSpec& parameterSpec,
                     const juce::String& stripPrefix)
        : spec(parameterSpec)
    {
        auto displayName = juce::String(spec.name);
        if (stripPrefix.isNotEmpty() && displayName.startsWith(stripPrefix))
            displayName = displayName.substring(stripPrefix.length());
        if (displayName.endsWith(" Direct")) // redundant inside the Direct Modulation panels
            displayName = displayName.dropLastCharacters(7);

        nameLabel.setText(displayName, juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, mutedText);
        nameLabel.setFont(uiFont(11.0f));
        nameLabel.setMinimumHorizontalScale(0.6f);
        addAndMakeVisible(nameLabel);

        if (spec.kind == synth::ParameterKind::Choice)
        {
            wide = true;
            for (int i = 0; i < static_cast<int>(spec.choices.size()); ++i)
                combo.addItem(juce::String(spec.choices[static_cast<std::size_t>(i)]), i + 1);

            comboAttachment = std::make_unique<ComboBoxAttachment>(state, spec.id, combo);
            addAndMakeVisible(combo);
        }
        else if (spec.kind == synth::ParameterKind::Bool)
        {
            toggle.setClickingTogglesState(true);
            buttonAttachment = std::make_unique<ButtonAttachment>(state, spec.id, toggle);
            addAndMakeVisible(toggle);
        }
        else
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters(rotaryStart, rotaryEnd, true);
            // Knobs bound to a TransMod destination become modulation drop
            // targets with halo rings; the catalog is the source of truth.
            for (const auto& destination : synth::modulationDestinationCatalog())
            {
                if (destination.targetParameterId == spec.id)
                {
                    slider.setDestinationInfo(&destination);
                    break;
                }
            }
            // Small recessed value readout under the knob.
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 15);
            slider.setColour(juce::Slider::textBoxTextColourId, text);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, fieldBg.withAlpha(0.55f));
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setNumDecimalPlacesToDisplay(2); // fallback so text never renders in scientific form
            tagSliderReadout(slider, spec.id, juce::String(spec.name));

            // The attachment must exist first: SliderParameterAttachment overwrites the
            // text<->value functions in its constructor, so our formatter is applied after.
            sliderAttachment = std::make_unique<SliderAttachment>(state, spec.id, slider);
            slider.textFromValueFunction = [this](double value) { return formatValue(spec, value); };
            slider.valueFromTextFunction = [this](const juce::String& textValue) { return parseValueText(spec, textValue); };
            slider.updateText();
            addAndMakeVisible(slider);
        }
    }

    bool isWide() const noexcept { return wide; }

    // Zone hue for this control's value arc, so each module's knobs carry the
    // rack's colour coding.
    void setAccentColour(juce::Colour colour)
    {
        if (!colour.isTransparent())
            slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
    }

    // Non-null when this control's knob is a modulation drop target.
    ModTargetSlider* modTarget() noexcept
    {
        return spec.kind == synth::ParameterKind::Float && slider.isModTarget() ? &slider : nullptr;
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        nameLabel.setBounds(bounds.removeFromTop(13));

        if (spec.kind == synth::ParameterKind::Choice)
            combo.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth() - 4, 28));
        else if (spec.kind == synth::ParameterKind::Bool)
            toggle.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth(), 30));
        else
            slider.setBounds(bounds);
    }

private:
    synth::ParameterSpec spec;
    bool wide = false;
    juce::Label nameLabel;
    ModTargetSlider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    std::unique_ptr<SliderAttachment> sliderAttachment;
    std::unique_ptr<ComboBoxAttachment> comboAttachment;
    std::unique_ptr<ButtonAttachment> buttonAttachment;
};

// ============================================================================
// Meter: peak output meter with clip latch.
// ============================================================================
class SynthAudioProcessorEditor::Meter final : public juce::Component
{
public:
    void setLevel(float peakLinear, bool flagClip)
    {
        const auto oldLitSegments = litSegmentsForPeak(peak);
        const auto oldClipVisible = clipHold > 0;

        peak = std::isfinite(peakLinear) ? peakLinear : 0.0f;
        if (flagClip || peak >= 0.98f)
            clipHold = 12; // hold the clip indicator for a few timer ticks
        else if (clipHold > 0)
            --clipHold;

        if (oldLitSegments != litSegmentsForPeak(peak) || oldClipVisible != (clipHold > 0))
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(fieldBg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(strokeSoft);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        auto area = bounds.reduced(3.0f);
        const auto db = peak > 0.0001f ? juce::Decibels::gainToDecibels(peak) : -100.0f;
        const auto norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);

        // Vertical LED ladder, like the Sylenth mixer output meter: green low, amber mid,
        // red top, lit from the bottom up to the current peak. The top LED latches on clip.
        constexpr int segments = 14;
        constexpr float segGap = 2.0f;
        const auto segHeight = (area.getHeight() - (segments - 1) * segGap) / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i)
        {
            const auto fraction = static_cast<float>(i + 1) / static_cast<float>(segments);
            const auto fi = static_cast<float>(i);
            const auto seg = juce::Rectangle<float>(area.getX(),
                                                    area.getBottom() - (fi + 1.0f) * segHeight - fi * segGap,
                                                    area.getWidth(), segHeight);
            const auto colour = fraction > 0.88f ? warn : (fraction > 0.6f ? staged : live);
            const auto lit = norm >= static_cast<float>(i) / static_cast<float>(segments)
                             || (i == segments - 1 && clipHold > 0);
            g.setColour(lit ? colour.withAlpha(0.95f) : colour.withAlpha(0.10f));
            g.fillRoundedRectangle(seg, 1.5f);
        }
    }

private:
    static int litSegmentsForPeak(float peakLinear)
    {
        const auto db = peakLinear > 0.0001f ? juce::Decibels::gainToDecibels(peakLinear) : -100.0f;
        const auto norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
        return juce::jlimit(0, 14, static_cast<int>(std::ceil(norm * 14.0f)));
    }

    float peak = 0.0f;
    int clipHold = 0;
};

// ============================================================================
// LcdDisplay: a glossy blue Sylenth-style screen showing the live preset.
//
// Read-only display of real state: the current preset name plus its source /
// bank / category, and the dirty flag. No new control or stored state.
// ============================================================================
class SynthAudioProcessorEditor::LcdDisplay final : public LayoutSection,
                                                    private juce::Timer
{
public:
    explicit LcdDisplay(SynthAudioProcessor& processor)
        : audioProcessor(processor)
    {
        startTimerHz(30);
    }

    int preferredHeight(int) const override { return 188; }

    void setPreset(const juce::String& name, const juce::String& detail)
    {
        if (name == presetName && detail == presetDetail)
            return;
        presetName = name;
        presetDetail = detail;
        repaint();
    }

    // Program-slot readout (e.g. "012 / 128") derived from the real preset list position.
    void setProgram(const juce::String& value)
    {
        if (value == programText)
            return;
        programText = value;
        repaint();
    }

    // Live voice/CPU/transport line, fed straight from the diagnostics snapshot.
    void setDiagnostics(const juce::String& value)
    {
        if (value == diagnostics)
            return;
        diagnostics = value;
        repaint();
    }

    void setDirty(bool isDirty)
    {
        if (isDirty == dirty)
            return;
        dirty = isDirty;
        repaint();
    }

    // Show a "Name = value" readout for a touched control, like Sylenth's centre display.
    // It holds for a short time and then reverts to the preset detail line.
    void setTouchReadout(const juce::String& readoutText)
    {
        touchReadout = readoutText;
        touchHold = 22; // ~1.4s at the editor's 15Hz tick
        repaint();
    }

    // Called from the editor timer; clears the touch readout after its hold elapses.
    void tickTouchReadout()
    {
        if (touchHold <= 0)
            return;

        --touchHold;
        if (touchHold == 0)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        // Flat bezel around a recessed glass screen — the surface's primary readout
        // hub (preset name, program slot, live state, output scope).
        auto bezel = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(panelBg);
        g.fillRoundedRectangle(bezel, 6.0f);
        g.setColour(strokeSoft);
        g.drawRoundedRectangle(bezel, 6.0f, 1.0f);

        auto screen = bezel.reduced(8.0f);
        g.setGradientFill(juce::ColourGradient(lcdBg.brighter(0.20f), 0.0f, screen.getY(),
                                               lcdBgEdge, 0.0f, screen.getBottom(), false));
        g.fillRoundedRectangle(screen, 5.0f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.fillRoundedRectangle(screen.withHeight(screen.getHeight() * 0.4f).reduced(3.0f, 2.0f), 4.0f);
        // Faint scanlines sell the backlit-LCD read; purely decorative, no state.
        g.setColour(juce::Colours::black.withAlpha(0.05f));
        for (float y = screen.getY() + 4.0f; y < screen.getBottom() - 2.0f; y += 3.0f)
            g.drawHorizontalLine(juce::roundToInt(y), screen.getX() + 2.0f, screen.getRight() - 2.0f);
        g.setColour(lcdStroke);
        g.drawRoundedRectangle(screen, 5.0f, 1.4f);

        auto content = screen.reduced(18.0f, 12.0f).toNearestInt();

        // Header row: PROGRAM label + slot index left, dirty pill right.
        auto header = content.removeFromTop(16);
        auto pill = header.removeFromRight(78).withSizeKeepingCentre(74, 15);
        g.setColour((dirty ? staged : live).withAlpha(0.24f));
        g.fillRoundedRectangle(pill.toFloat(), 7.0f);
        g.setColour(dirty ? staged : live);
        g.setFont(lcdFont(9.5f, true));
        g.drawText(dirty ? "EDITED" : "CLEAN", pill, juce::Justification::centred, false);

        g.setColour(lcdDim);
        g.setFont(lcdFont(10.0f, true));
        g.drawText("PROGRAM", header.removeFromLeft(74), juce::Justification::centredLeft, false);
        if (programText.isNotEmpty())
        {
            g.setColour(lcdText);
            g.setFont(lcdFont(11.0f, true));
            g.drawText(programText, header, juce::Justification::centredLeft, false);
        }

        // Bottom readout strip: live voice/CPU/transport state from the diagnostics snapshot.
        auto readout = content.removeFromBottom(16);
        g.setColour(lcdStroke.withAlpha(0.55f));
        g.drawHorizontalLine(readout.getY() - 4, static_cast<float>(content.getX()),
                             static_cast<float>(content.getRight()));
        g.setColour(lcdDim);
        g.setFont(lcdFont(10.5f, true));
        g.drawText(diagnostics, readout, juce::Justification::centredLeft, false);

        // Name + detail block fills the space between the header and the readout strip.
        auto block = content.reduced(0, 4);
        auto nameArea = block.removeFromTop(juce::jmin(42, juce::jmax(24, block.getHeight() - 22)));

        // Live output scope behind the text: the real rendered signal pulled from
        // the processor's lock-free ring, drawn as a faint trace so the display
        // breathes with the sound (flat centre line when idle).
        {
            const auto trace = juce::Rectangle<float>(static_cast<float>(content.getX()),
                                                      static_cast<float>(nameArea.getY()),
                                                      static_cast<float>(content.getWidth()),
                                                      static_cast<float>(block.getBottom() - nameArea.getY()));
            const auto midY = trace.getCentreY();
            juce::Path wave;
            if (scopeCount > 1)
            {
                for (int i = 0; i < scopeCount; ++i)
                {
                    const auto x = trace.getX()
                        + trace.getWidth() * static_cast<float>(i) / static_cast<float>(scopeCount - 1);
                    const auto sample = juce::jlimit(-1.0f, 1.0f, scope[static_cast<std::size_t>(i)]);
                    const auto y = midY - sample * trace.getHeight() * 0.48f;
                    if (i == 0)
                        wave.startNewSubPath(x, y);
                    else
                        wave.lineTo(x, y);
                }
                g.setColour(lcdText.withAlpha(0.30f));
                g.strokePath(wave, juce::PathStrokeType(1.2f));
            }
            else
            {
                g.setColour(lcdText.withAlpha(0.14f));
                g.drawHorizontalLine(juce::roundToInt(midY), trace.getX(), trace.getRight());
            }
        }

        g.setColour(lcdText);
        g.setFont(lcdFont(26.0f, true));
        g.drawFittedText(presetName.isNotEmpty() ? presetName : "Init", nameArea,
                         juce::Justification::centredLeft, 1, 0.5f);

        block.removeFromTop(4);
        if (touchHold > 0 && touchReadout.isNotEmpty())
        {
            // A control is being touched: show its live name = value in bright LCD text.
            g.setColour(lcdText);
            g.setFont(lcdFont(14.0f, true));
            g.drawFittedText(touchReadout, block, juce::Justification::centredLeft, 1, 0.6f);
        }
        else
        {
            g.setColour(lcdDim);
            g.setFont(lcdFont(12.0f));
            g.drawFittedText(presetDetail, block, juce::Justification::centredLeft, 1, 0.7f);
        }
    }

private:
    void timerCallback() override
    {
        if (!isShowing())
            return;
        scopeCount = audioProcessor.readScopeSamples(scope.data(), static_cast<int>(scope.size()));
        repaint();
    }

    SynthAudioProcessor& audioProcessor;
    std::array<float, 512> scope {};
    int scopeCount = 0;
    juce::String presetName { "Init" };
    juce::String presetDetail { "Unsaved session" };
    juce::String programText;
    juce::String diagnostics { "VOICES 0/0   LOAD 0%" };
    juce::String touchReadout;
    int touchHold = 0;
    bool dirty = false;
};

// ============================================================================
// MixerPanel: the Sylenth MIXER — Mix A / Mix B / Main Vol over the real
// layer/amp level parameters, plus the shared output LED meter ladder.
// ============================================================================
class SynthAudioProcessorEditor::MixerPanel final : public LayoutSection
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    MixerPanel(juce::AudioProcessorValueTreeState& state, Meter& outputMeter)
        : meter(outputMeter)
    {
        addKnob(state, "layer.1.level_db", "MIX A");
        addKnob(state, "layer.2.level_db", "MIX B");
        addKnob(state, "amp.level_db", "MAIN");
        addAndMakeVisible(meter);
    }

    int preferredHeight(int) const override { return 156; }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);
        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zonePerform);
        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("MIXER", getLocalBounds().removeFromTop(headerHeight).reduced(16, 0),
                   juce::Justification::centred, true);

        const auto areas = computeAreas();
        const auto cols = knobColumns(areas.knobs);
        g.setFont(uiFont(10.0f, true));
        g.setColour(mutedText);
        for (std::size_t i = 0; i < knobs.size(); ++i)
            if (knobs[i].ok)
                g.drawText(knobs[i].label, cols[i].withY(areas.labels.getY()).withHeight(areas.labels.getHeight()),
                           juce::Justification::centred, false);
        g.drawText("OUT", areas.meterLabel, juce::Justification::centred, false);
    }

    void resized() override
    {
        const auto areas = computeAreas();
        const auto cols = knobColumns(areas.knobs);
        for (std::size_t i = 0; i < knobs.size(); ++i)
            if (knobs[i].ok)
                knobs[i].slider.setBounds(cols[i]);
        meter.setBounds(areas.meter.withSizeKeepingCentre(14, areas.meter.getHeight()));
    }

private:
    static constexpr int headerHeight = 26;
    static constexpr int padX = 12;
    static constexpr int padTop = 6;
    static constexpr int padBottom = 8;
    static constexpr int knobGap = 6;
    static constexpr int knobCount = 3;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
        juce::String label;
        bool ok = false;
    };

    struct Areas
    {
        juce::Rectangle<int> labels, knobs, meter, meterLabel;
    };

    Areas computeAreas() const
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(padX, 0);
        content.removeFromTop(padTop);
        content.removeFromBottom(padBottom);

        Areas a;
        auto meterCol = content.removeFromRight(24);
        content.removeFromRight(10);
        a.meterLabel = meterCol.removeFromTop(13);
        a.meter = meterCol;
        a.labels = content.removeFromTop(14);
        a.knobs = content;
        return a;
    }

    std::array<juce::Rectangle<int>, knobCount> knobColumns(juce::Rectangle<int> area) const
    {
        const auto columnWidth = (area.getWidth() - (knobCount - 1) * knobGap) / knobCount;
        std::array<juce::Rectangle<int>, knobCount> cols;
        for (int i = 0; i < knobCount; ++i)
            cols[static_cast<std::size_t>(i)] =
                juce::Rectangle<int>(area.getX() + i * (columnWidth + knobGap), area.getY(),
                                     columnWidth, area.getHeight());
        return cols;
    }

    void addKnob(juce::AudioProcessorValueTreeState& state, const std::string& id, const juce::String& label)
    {
        auto& knob = knobs[static_cast<std::size_t>(nextKnob++)];
        const auto* found = synth::findParameterSpec(id);
        if (found == nullptr)
            return;

        const auto spec = *found;
        knob.label = label;
        knob.ok = true;
        knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setRotaryParameters(rotaryStart, rotaryEnd, true);
        tagSliderReadout(knob.slider, id, label);
        // Match the recessed value-box treatment used by every other knob (ParameterControl).
        knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 15);
        knob.slider.setColour(juce::Slider::textBoxTextColourId, text);
        knob.slider.setColour(juce::Slider::textBoxBackgroundColourId, fieldBg.withAlpha(0.55f));
        knob.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.attachment = std::make_unique<SliderAttachment>(state, id, knob.slider);
        knob.slider.textFromValueFunction = [spec](double value) { return formatValue(spec, value); };
        knob.slider.valueFromTextFunction = [spec](const juce::String& t) { return parseValueText(spec, t); };
        knob.slider.updateText();
        addAndMakeVisible(knob.slider);
    }

    Meter& meter;
    std::array<Knob, knobCount> knobs;
    int nextKnob = 0;
};

// ============================================================================
// Panel: titled frame that grid-packs controls into stable rows.
// ============================================================================
class SynthAudioProcessorEditor::Panel final : public LayoutSection
{
public:
    Panel(juce::AudioProcessorValueTreeState& state,
          juce::String panelTitle,
          const std::vector<std::string>& ids,
          juce::String badgeText,
          juce::Colour badgeFill,
          const juce::String& stripPrefix,
          const juce::String& enabledParamId = {})
        : title(std::move(panelTitle)),
          badge(std::move(badgeText)),
          badgeColour(badgeFill)
    {
        if (enabledParamId.isNotEmpty())
        {
            enabledParam = state.getRawParameterValue(enabledParamId);
            lastEnabledState = isModuleEnabled();
        }

        for (const auto& id : ids)
        {
            if (const auto* found = synth::findParameterSpec(id))
            {
                auto control = std::make_unique<ParameterControl>(state, *found, stripPrefix);
                control->setAccentColour(badgeColour);
                addAndMakeVisible(*control);
                controls.push_back(std::move(control));
            }
        }
    }

    int preferredHeight(int width) const override
    {
        return computeLayout(width, false);
    }

    // Gathers the knobs that accept modulation drops so the editor can wire
    // route callbacks and push halo state.
    void collectModTargets(std::vector<ModTargetSlider*>& out)
    {
        for (auto& control : controls)
            if (auto* target = control->modTarget())
                out.push_back(target);
    }

    // True when this panel carries a module power toggle that drives its dimmed state.
    bool hasEnabledState() const noexcept { return enabledParam != nullptr; }

    // UI-thread read of the bound enable atomic; modules with no toggle read as on.
    bool isModuleEnabled() const noexcept
    {
        return enabledParam == nullptr || enabledParam->load() >= 0.5f;
    }

    // Called from the editor timer for the visible page only. Repaints the panel when
    // its power toggle flips so the header reflects bypass without per-frame churn.
    void syncEnabledState()
    {
        if (enabledParam == nullptr)
            return;

        const auto enabled = isModuleEnabled();
        if (enabled != lastEnabledState)
        {
            lastEnabledState = enabled;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto hasState = hasEnabledState();
        const auto on = isModuleEnabled();

        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea, !hasState || on);

        // Zone identity tick: badgeColour doubles as the functional-zone hue.
        const auto tickColour = badgeColour.isTransparent() ? accent.withAlpha(0.65f) : badgeColour;
        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), tickColour, !hasState || on);

        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);

        // Power dot: a scan-friendly on/off indicator for modules with an enable toggle.
        if (hasState)
        {
            auto dotArea = titleArea.removeFromLeft(14);
            const auto dot = juce::Rectangle<float>(0.0f, 0.0f, 7.0f, 7.0f)
                                 .withCentre(dotArea.toFloat().getCentre());
            g.setColour(on ? live : stroke);
            g.fillEllipse(dot);
        }

        juce::Rectangle<int> badgeArea;
        if (badge.isNotEmpty())
        {
            const auto badgeWidth = badge.length() * 8 + 16;
            badgeArea = titleArea.removeFromRight(badgeWidth).withSizeKeepingCentre(badgeWidth, 16);
            titleArea.removeFromRight(8);
        }

        g.setColour(hasState && !on ? mutedText : text);
        g.setFont(uiFont(12.0f, true));
        g.drawText(title.toUpperCase(), titleArea, juce::Justification::centred, true);

        if (badge.isNotEmpty())
        {
            const auto badgeActive = !hasState || on;
            g.setColour(badgeActive ? badgeColour.withAlpha(0.18f) : strokeSoft.withAlpha(0.35f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
            g.setColour(badgeActive ? badgeColour : mutedText);
            g.setFont(uiFont(10.0f, true));
            g.drawText(badge, badgeArea, juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        computeLayout(getWidth(), true);
    }

private:
    int computeLayout(int width, bool apply) const
    {
        if (width <= 0)
            return headerHeight + padTop + cellHeight + padBottom;

        const auto innerX = padX;
        const auto innerWidth = width - 2 * padX;
        const auto columns = std::max(1, (innerWidth + gap) / (unitWidth + gap));
        const auto columnWidth = static_cast<float>(innerWidth - (columns - 1) * gap) / static_cast<float>(columns);
        const auto topOffset = headerHeight + padTop;

        int column = 0;
        int row = 0;
        for (const auto& control : controls)
        {
            const auto span = std::min(control->isWide() ? 2 : 1, columns);
            if (column + span > columns)
            {
                column = 0;
                ++row;
            }

            if (apply)
            {
                const auto cellX = innerX + static_cast<int>(std::round(static_cast<float>(column) * (columnWidth + static_cast<float>(gap))));
                const auto cellW = static_cast<int>(std::round(static_cast<float>(span) * columnWidth + static_cast<float>((span - 1) * gap)));
                const auto cellY = topOffset + row * (cellHeight + rowGap);
                control->setBounds(cellX, cellY, cellW, cellHeight);
            }

            column += span;
        }

        const auto rowCount = controls.empty() ? 0 : row + 1;
        return topOffset + rowCount * cellHeight + std::max(0, rowCount - 1) * rowGap + padBottom;
    }

    static constexpr int headerHeight = 26;
    static constexpr int unitWidth = 66;
    static constexpr int cellHeight = 70;
    static constexpr int gap = 7;
    static constexpr int rowGap = 4;
    static constexpr int padX = 11;
    static constexpr int padTop = 6;
    static constexpr int padBottom = 9;

    juce::String title;
    juce::String badge;
    juce::Colour badgeColour;
    std::atomic<float>* enabledParam = nullptr;
    bool lastEnabledState = true;
    std::vector<std::unique_ptr<ParameterControl>> controls;
};

// ============================================================================
// OscillatorPanel: a faithful Sylenth OSCILLATOR module. A PITCH sub-box
// (OCTAVE / NOTE inc-dec boxes + a FINE knob) sits left of a VOLUME PHASE
// DETUNE STEREO PAN knob row over an INV / WAVE / VOICES / RETRIG control row,
// mirroring the hardware module. Every control binds to a real
// layer.N.osc.M.* parameter; nothing here is decorative.
// ============================================================================
class SynthAudioProcessorEditor::OscillatorPanel final : public LayoutSection
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    OscillatorPanel(juce::AudioProcessorValueTreeState& state,
                    const juce::String& prefix, juce::String panelTitle,
                    const juce::String& enabledId)
        : title(std::move(panelTitle))
    {
        if (enabledId.isNotEmpty())
        {
            enabledParam = state.getRawParameterValue(enabledId);
            lastEnabled = isModuleEnabled();
            enableToggle.setClickingTogglesState(true);
            buttonAttachments.push_back(std::make_unique<ButtonAttachment>(state, enabledId, enableToggle));
            addAndMakeVisible(enableToggle);
        }

        setupIncDec(octave,    state, (prefix + "octave").toStdString());
        setupIncDec(note,      state, (prefix + "note").toStdString());
        setupIncDec(voicesCtl, state, (prefix + "voices").toStdString());
        setupKnob(fine,   state, (prefix + "fine_cents").toStdString());
        setupKnob(volume, state, (prefix + "level").toStdString());
        setupKnob(phase,  state, (prefix + "phase_degrees").toStdString());
        setupKnob(detune, state, (prefix + "detune").toStdString());
        setupKnob(stereo, state, (prefix + "stereo").toStdString());
        setupKnob(pan,    state, (prefix + "pan").toStdString());
        setupCombo(wave,  state, (prefix + "waveform").toStdString());
        setupToggle(invert, state, (prefix + "invert").toStdString());
        setupToggle(retrig, state, (prefix + "retrigger").toStdString());
    }

    int preferredHeight(int) const override { return 158; }

    bool hasEnabledState() const noexcept { return enabledParam != nullptr; }
    bool isModuleEnabled() const noexcept { return enabledParam == nullptr || enabledParam->load() >= 0.5f; }
    void syncEnabledState()
    {
        if (enabledParam == nullptr)
            return;
        const auto on = isModuleEnabled();
        if (on != lastEnabled) { lastEnabled = on; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        const auto on = isModuleEnabled();
        paintPanelBody(g, getLocalBounds().toFloat().reduced(0.5f));
        auto header = getLocalBounds().removeFromTop(captionH);
        paintCaptionBar(g, header, on);
        g.setColour(on ? text : mutedText);
        g.setFont(uiFont(12.0f, true));
        g.drawText(title.toUpperCase(), header.reduced(16, 0), juce::Justification::centred, true);

        // PITCH sub-box: a recessed framed group, like Sylenth's pitch cluster.
        g.setColour(fieldBg.withAlpha(0.45f));
        g.fillRoundedRectangle(pitchBox.toFloat(), 2.5f);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(pitchBox.toFloat().reduced(0.5f), 2.5f, 1.0f);

        g.setColour(mutedText);
        g.setFont(uiFont(9.5f, true));
        for (const auto& l : labels)
            g.drawText(l.second, l.first, juce::Justification::centred, false);
    }

    void resized() override
    {
        labels.clear();
        auto area = getLocalBounds();
        auto header = area.removeFromTop(captionH);
        if (enabledParam != nullptr)
            enableToggle.setBounds(header.removeFromRight(36).withSizeKeepingCentre(24, 15));

        area = area.reduced(9, 0);
        area.removeFromTop(5);
        area.removeFromBottom(8);

        // PITCH sub-box on the left: OCTAVE / NOTE inc-dec boxes, then a FINE knob.
        pitchBox = area.removeFromLeft(88);
        area.removeFromLeft(8);
        {
            auto p = pitchBox.reduced(5, 4);
            labels.push_back({ p.removeFromTop(12), "PITCH" });
            labels.push_back({ p.removeFromTop(9), "OCT" });
            octave.setBounds(p.removeFromTop(19));
            p.removeFromTop(2);
            labels.push_back({ p.removeFromTop(9), "NOTE" });
            note.setBounds(p.removeFromTop(19));
            p.removeFromTop(3);
            labels.push_back({ p.removeFromTop(9), "FINE" });
            fine.setBounds(p.withSizeKeepingCentre(juce::jmin(40, p.getWidth()), p.getHeight()));
        }

        // Right area: a five-knob row over a four-cell control row.
        auto bottom = area.removeFromBottom(40);
        auto knobRow = area;
        const auto kw = knobRow.getWidth() / 5;
        auto placeKnob = [&](juce::Slider& s, const juce::String& name, bool last) {
            auto c = last ? knobRow : knobRow.removeFromLeft(kw);
            labels.push_back({ c.removeFromBottom(12), name });
            const auto d = juce::jmin(48, juce::jmin(c.getWidth() - 6, c.getHeight()));
            s.setBounds(c.withSizeKeepingCentre(d, d));
        };
        placeKnob(volume, "VOLUME", false);
        placeKnob(phase,  "PHASE",  false);
        placeKnob(detune, "DETUNE", false);
        placeKnob(stereo, "STEREO", false);
        placeKnob(pan,    "PAN",    true);

        const auto bw = bottom.getWidth() / 4;
        auto placeBottom = [&](juce::Component& comp, const juce::String& name, int w, bool centredToggle) {
            auto c = (w == 0) ? bottom : bottom.removeFromLeft(w);
            labels.push_back({ c.removeFromBottom(11), name });
            if (centredToggle)
                comp.setBounds(c.withSizeKeepingCentre(juce::jmin(42, c.getWidth() - 4), juce::jmin(20, c.getHeight())));
            else
                comp.setBounds(c.reduced(3, 1));
        };
        placeBottom(invert,    "INV",    bw, true);
        placeBottom(wave,      "WAVE",   bw, false);
        placeBottom(voicesCtl, "VOICES", bw, false);
        placeBottom(retrig,    "RETRIG", 0,  true);
    }

private:
    void setupKnob(juce::Slider& s, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        const auto* spec = synth::findParameterSpec(id);
        if (spec == nullptr)
            return;
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(rotaryStart, rotaryEnd, true);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0); // osc knobs label only
        s.setColour(juce::Slider::rotarySliderFillColourId, zoneSource);
        tagSliderReadout(s, id, juce::String(spec->name));
        sliderAttachments.push_back(std::make_unique<SliderAttachment>(state, id, s));
        const auto sp = *spec;
        s.textFromValueFunction = [sp](double v) { return formatValue(sp, v); };
        s.updateText();
        addAndMakeVisible(s);
    }

    void setupIncDec(juce::Slider& s, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        const auto* spec = synth::findParameterSpec(id);
        if (spec == nullptr)
            return;
        s.setSliderStyle(juce::Slider::IncDecButtons);
        s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 30, 19);
        s.setColour(juce::Slider::textBoxTextColourId, text);
        s.setColour(juce::Slider::textBoxBackgroundColourId, fieldBg);
        s.setColour(juce::Slider::textBoxOutlineColourId, strokeSoft.withAlpha(0.7f));
        tagSliderReadout(s, id, juce::String(spec->name));
        sliderAttachments.push_back(std::make_unique<SliderAttachment>(state, id, s));
        // Compact integer readout: Sylenth's OCT/NOTE/VOICES boxes show a number, not a unit.
        s.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)); };
        s.valueFromTextFunction = [](const juce::String& t) { return static_cast<double>(t.getIntValue()); };
        s.updateText();
        addAndMakeVisible(s);
    }

    void setupCombo(juce::ComboBox& c, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        const auto* spec = synth::findParameterSpec(id);
        if (spec == nullptr)
            return;
        for (int i = 0; i < static_cast<int>(spec->choices.size()); ++i)
            c.addItem(juce::String(spec->choices[static_cast<std::size_t>(i)]), i + 1);
        comboAttachments.push_back(std::make_unique<ComboBoxAttachment>(state, id, c));
        addAndMakeVisible(c);
    }

    void setupToggle(juce::ToggleButton& t, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        if (synth::findParameterSpec(id) == nullptr)
            return;
        t.setClickingTogglesState(true);
        buttonAttachments.push_back(std::make_unique<ButtonAttachment>(state, id, t));
        addAndMakeVisible(t);
    }

    static constexpr int captionH = 26;

    juce::String title;
    std::atomic<float>* enabledParam = nullptr;
    bool lastEnabled = true;

    juce::Slider octave, note, voicesCtl, fine, volume, phase, detune, stereo, pan;
    juce::ComboBox wave;
    juce::ToggleButton invert, retrig, enableToggle;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<int> pitchBox;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};

// ============================================================================
// FilterPanel: the Sylenth FILTER module — a TYPE selector over a CUTOFF /
// RESONANCE / DRIVE / KEYTRACK knob row with a quality (oversampling) selector.
// Bound to the real filter.* parameters; the enable LED rides the caption.
// ============================================================================
class SynthAudioProcessorEditor::FilterPanel final : public LayoutSection
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    explicit FilterPanel(juce::AudioProcessorValueTreeState& state)
    {
        enabledParam = state.getRawParameterValue("filter.enabled");
        if (enabledParam != nullptr)
        {
            lastEnabled = isModuleEnabled();
            enableToggle.setClickingTogglesState(true);
            buttonAttachments.push_back(std::make_unique<ButtonAttachment>(state, "filter.enabled", enableToggle));
            addAndMakeVisible(enableToggle);
        }
        setupCombo(typeBox,    state, "filter.mode");
        setupCombo(qualityBox, state, "filter.oversampling");
        setupKnob(cutoff,    state, "filter.cutoff_semitones");
        setupKnob(resonance, state, "filter.resonance");
        setupKnob(drive,     state, "filter.drive");
        setupKnob(keytrack,  state, "filter.keytrack");
    }

    int preferredHeight(int) const override { return 158; }

    bool hasEnabledState() const noexcept { return enabledParam != nullptr; }
    bool isModuleEnabled() const noexcept { return enabledParam == nullptr || enabledParam->load() >= 0.5f; }
    void syncEnabledState()
    {
        if (enabledParam == nullptr)
            return;
        const auto on = isModuleEnabled();
        if (on != lastEnabled) { lastEnabled = on; repaint(); }
    }

    void collectModTargets(std::vector<ModTargetSlider*>& out)
    {
        for (auto* knob : { &cutoff, &resonance, &drive, &keytrack })
            if (knob->isModTarget())
                out.push_back(knob);
    }

    void paint(juce::Graphics& g) override
    {
        const auto on = isModuleEnabled();
        paintPanelBody(g, getLocalBounds().toFloat().reduced(0.5f));
        auto header = getLocalBounds().removeFromTop(captionH);
        paintCaptionBar(g, header, on);
        g.setColour(on ? text : mutedText);
        g.setFont(uiFont(12.0f, true));
        g.drawText("FILTER", header.reduced(16, 0), juce::Justification::centred, true);

        g.setColour(mutedText);
        g.setFont(uiFont(9.5f, true));
        for (const auto& l : labels)
            g.drawText(l.second, l.first, juce::Justification::centred, false);
    }

    void resized() override
    {
        labels.clear();
        auto area = getLocalBounds();
        auto header = area.removeFromTop(captionH);
        if (enabledParam != nullptr)
            enableToggle.setBounds(header.removeFromRight(36).withSizeKeepingCentre(24, 15));

        area = area.reduced(10, 0);
        area.removeFromTop(6);
        area.removeFromBottom(8);

        // TYPE selector across the top.
        {
            auto row = area.removeFromTop(30);
            labels.push_back({ row.removeFromLeft(42), "TYPE" });
            typeBox.setBounds(row.reduced(2, 2));
        }
        area.removeFromTop(4);

        // Quality (oversampling) selector across the bottom.
        {
            auto row = area.removeFromBottom(28);
            labels.push_back({ row.removeFromLeft(58), "QUALITY" });
            qualityBox.setBounds(row.reduced(2, 1));
        }

        // CUTOFF / RESONANCE / DRIVE / KEYTRACK knob row fills the middle.
        auto knobRow = area;
        const auto kw = knobRow.getWidth() / 4;
        auto placeKnob = [&](juce::Slider& s, const juce::String& name, bool last) {
            auto c = last ? knobRow : knobRow.removeFromLeft(kw);
            labels.push_back({ c.removeFromBottom(12), name });
            const auto d = juce::jmin(50, juce::jmin(c.getWidth() - 6, c.getHeight()));
            s.setBounds(c.withSizeKeepingCentre(d, d));
        };
        placeKnob(cutoff,    "CUTOFF",   false);
        placeKnob(resonance, "RESO",     false);
        placeKnob(drive,     "DRIVE",    false);
        placeKnob(keytrack,  "KEYTRACK", true);
    }

private:
    void setupKnob(ModTargetSlider& s, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        const auto* spec = synth::findParameterSpec(id);
        if (spec == nullptr)
            return;
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(rotaryStart, rotaryEnd, true);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setColour(juce::Slider::rotarySliderFillColourId, zoneShape);
        for (const auto& destination : synth::modulationDestinationCatalog())
        {
            if (destination.targetParameterId == id)
            {
                s.setDestinationInfo(&destination);
                break;
            }
        }
        tagSliderReadout(s, id, juce::String(spec->name));
        sliderAttachments.push_back(std::make_unique<SliderAttachment>(state, id, s));
        const auto sp = *spec;
        s.textFromValueFunction = [sp](double v) { return formatValue(sp, v); };
        s.updateText();
        addAndMakeVisible(s);
    }

    void setupCombo(juce::ComboBox& c, juce::AudioProcessorValueTreeState& state, const std::string& id)
    {
        const auto* spec = synth::findParameterSpec(id);
        if (spec == nullptr)
            return;
        for (int i = 0; i < static_cast<int>(spec->choices.size()); ++i)
            c.addItem(juce::String(spec->choices[static_cast<std::size_t>(i)]), i + 1);
        comboAttachments.push_back(std::make_unique<ComboBoxAttachment>(state, id, c));
        addAndMakeVisible(c);
    }

    static constexpr int captionH = 26;

    std::atomic<float>* enabledParam = nullptr;
    bool lastEnabled = true;

    ModTargetSlider cutoff, resonance, drive, keytrack;
    juce::ComboBox typeBox, qualityBox;
    juce::ToggleButton enableToggle;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};

// ============================================================================
// EnvelopePanel: ADSR as vertical faders with a derived contour preview.
//
// This is a Sylenth-style envelope module: the four real APVTS parameters
// (attack / decay / sustain / release) are bound to vertical faders, and a
// read-only contour drawn above them is computed from the live fader values.
// The contour is a derived visualization, not a new control or stored state.
// ============================================================================
class SynthAudioProcessorEditor::EnvelopePanel final : public LayoutSection
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    EnvelopePanel(juce::AudioProcessorValueTreeState& state,
                  juce::String panelTitle,
                  juce::Colour zoneColour,
                  const juce::String& prefix)
        : title(std::move(panelTitle)), zone(zoneColour)
    {
        struct StageDef { const char* suffix; const char* label; };
        const std::array<StageDef, stageCount> defs { {
            { "attack_ms", "A" }, { "decay_ms", "D" }, { "sustain", "S" }, { "release_ms", "R" }
        } };

        for (std::size_t i = 0; i < defs.size(); ++i)
        {
            const auto id = (prefix + defs[i].suffix).toStdString();
            const auto* found = synth::findParameterSpec(id);
            if (found == nullptr)
                continue;

            auto& stage = stages[i];
            stage.label = defs[i].label;
            stage.spec = *found;
            stage.hasSpec = true;

            auto& slider = stage.slider;
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 14);
            slider.setColour(juce::Slider::trackColourId, zone.withAlpha(0.88f));
            slider.setColour(juce::Slider::backgroundColourId, fieldBg);
            slider.setColour(juce::Slider::thumbColourId, text);
            slider.setColour(juce::Slider::textBoxTextColourId, text);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

            const auto spec = stage.spec;
            tagSliderReadout(slider, id, juce::String(spec.name));
            stage.attachment = std::make_unique<SliderAttachment>(state, id, slider);
            slider.textFromValueFunction = [spec](double value) { return formatValue(spec, value); };
            slider.valueFromTextFunction = [spec](const juce::String& textValue) {
                return parseValueText(spec, textValue);
            };
            slider.updateText();
            slider.onValueChange = [this] { repaint(); }; // keep the derived contour in sync
            addAndMakeVisible(slider);
        }
    }

    int preferredHeight(int) const override { return 158; }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);
        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zone);

        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(46).withSizeKeepingCentre(46, 16);
        titleArea.removeFromRight(8);
        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText(title.toUpperCase(), titleArea, juce::Justification::centred, true);

        g.setColour(zone.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(zone);
        g.setFont(uiFont(10.0f, true));
        g.drawText("ADSR", badgeArea, juce::Justification::centred, false);

        const auto areas = computeAreas();
        paintContour(g, areas.contour);

        // Stage letters sit above each fader so the column reads A D S R like the hardware.
        g.setFont(uiFont(10.0f, true));
        const auto columns = faderColumns(areas.faders);
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (!stages[i].hasSpec)
                continue;
            g.setColour(mutedText);
            g.drawText(stages[i].label, columns[i].withY(areas.labels.getY()).withHeight(areas.labels.getHeight()),
                       juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        const auto areas = computeAreas();
        const auto columns = faderColumns(areas.faders);
        for (std::size_t i = 0; i < stages.size(); ++i)
        {
            if (stages[i].hasSpec)
                stages[i].slider.setBounds(columns[i]);
        }
    }

private:
    static constexpr int stageCount = 4;
    static constexpr int headerHeight = 26;
    static constexpr int padX = 12;
    static constexpr int padTop = 8;
    static constexpr int padBottom = 8;
    static constexpr int contourHeight = 44;
    static constexpr int labelHeight = 13;
    static constexpr int faderGap = 8;

    struct Stage
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
        synth::ParameterSpec spec;
        juce::String label;
        bool hasSpec = false;
    };

    struct Areas
    {
        juce::Rectangle<int> contour;
        juce::Rectangle<int> labels;
        juce::Rectangle<int> faders;
    };

    Areas computeAreas() const
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(padX, 0);
        content.removeFromTop(padTop);
        content.removeFromBottom(padBottom);

        Areas areas;
        areas.contour = content.removeFromTop(contourHeight);
        content.removeFromTop(6);
        areas.labels = content.removeFromTop(labelHeight);
        areas.faders = content;
        return areas;
    }

    std::array<juce::Rectangle<int>, stageCount> faderColumns(juce::Rectangle<int> faders) const
    {
        const auto columnWidth = (faders.getWidth() - (stageCount - 1) * faderGap) / stageCount;
        std::array<juce::Rectangle<int>, stageCount> columns;
        for (int i = 0; i < stageCount; ++i)
            columns[static_cast<std::size_t>(i)] =
                juce::Rectangle<int>(faders.getX() + i * (columnWidth + faderGap), faders.getY(),
                                     columnWidth, faders.getHeight());
        return columns;
    }

    // Derived ADSR contour from the live fader values. Times are compressed with a square
    // root so short and long stages stay legible; the sustain hold is a fixed segment.
    void paintContour(juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto frame = area.toFloat().reduced(0.5f);
        g.setColour(fieldBg);
        g.fillRoundedRectangle(frame, 4.0f);
        g.setColour(strokeSoft.withAlpha(0.8f));
        g.drawRoundedRectangle(frame, 4.0f, 1.0f);

        if (!stages[0].hasSpec || !stages[1].hasSpec || !stages[2].hasSpec || !stages[3].hasSpec)
            return;

        const auto plot = area.toFloat().reduced(7.0f, 6.0f);
        const auto compress = [](double ms) { return std::sqrt(juce::jmax(0.0, ms)); };
        const auto attack = compress(stages[0].slider.getValue());
        const auto decay = compress(stages[1].slider.getValue());
        const auto release = compress(stages[3].slider.getValue());
        const auto sustain = juce::jlimit(0.0, 1.0, stages[2].slider.getValue());

        auto timeSum = attack + decay + release;
        if (timeSum <= 0.0)
            timeSum = 1.0;

        const auto holdFraction = 0.18f;
        const auto dynamicWidth = plot.getWidth() * (1.0f - holdFraction);
        const auto attackWidth = dynamicWidth * static_cast<float>(attack / timeSum);
        const auto decayWidth = dynamicWidth * static_cast<float>(decay / timeSum);
        const auto releaseWidth = dynamicWidth * static_cast<float>(release / timeSum);
        const auto holdWidth = plot.getWidth() * holdFraction;

        const auto yFor = [&plot](double level) {
            return plot.getBottom() - static_cast<float>(level) * plot.getHeight();
        };

        const juce::Point<float> start(plot.getX(), yFor(0.0));
        const juce::Point<float> peak(start.x + attackWidth, yFor(1.0));
        const juce::Point<float> sustainStart(peak.x + decayWidth, yFor(sustain));
        const juce::Point<float> sustainEnd(sustainStart.x + holdWidth, yFor(sustain));
        const juce::Point<float> end(sustainEnd.x + releaseWidth, yFor(0.0));

        juce::Path contour;
        contour.startNewSubPath(start);
        contour.lineTo(peak);
        contour.lineTo(sustainStart);
        contour.lineTo(sustainEnd);
        contour.lineTo(end);

        juce::Path fill = contour;
        fill.lineTo(end.x, plot.getBottom());
        fill.lineTo(start.x, plot.getBottom());
        fill.closeSubPath();
        g.setColour(zone.withAlpha(0.16f));
        g.fillPath(fill);

        g.setColour(zone);
        g.strokePath(contour, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // Sustain-level guide so the held portion reads as a level, not a slope.
        g.setColour(zone.withAlpha(0.3f));
        g.drawHorizontalLine(juce::roundToInt(yFor(sustain)), sustainStart.x, sustainEnd.x);
    }

    juce::String title;
    juce::Colour zone;
    std::array<Stage, stageCount> stages;
};

// ============================================================================
// LfoModulePanel: the LFO as a visual instrument, not a form.
//
// Controls bind to the real lfo.* parameters on the left; the right half is a
// live display. Classic shapes render their waveform; the Step shape becomes a
// drawable 16-bar grid (drag to draw, double-click a bar to zero it) bound to
// the real lfo.step.* parameters. A playhead and value dot ride the display
// from the processor's lock-free visual feed whenever voices are sounding.
// ============================================================================
class SynthAudioProcessorEditor::LfoModulePanel final : public LayoutSection,
                                                        private juce::Timer
{
public:
    explicit LfoModulePanel(SynthAudioProcessor& processor)
        : audioProcessor(processor)
    {
        auto& state = audioProcessor.getValueTreeState();
        for (const auto* id : { "lfo.shape", "lfo.rate_mode", "lfo.rate_hz", "lfo.sync_division",
                                "lfo.phase_degrees", "lfo.gate_mode", "lfo.mono", "lfo.swing",
                                "lfo.step_count", "lfo.step_smooth" })
        {
            if (const auto* spec = synth::findParameterSpec(id))
            {
                auto control = std::make_unique<ParameterControl>(state, *spec, "LFO ");
                control->setAccentColour(zoneShape);
                addAndMakeVisible(*control);
                controls.push_back(std::move(control));
            }
        }

        shapeValue = state.getRawParameterValue("lfo.shape");
        phaseValue = state.getRawParameterValue("lfo.phase_degrees");
        stepCountValue = state.getRawParameterValue("lfo.step_count");
        for (int step = 0; step < synth::lfoStepSlotCount; ++step)
        {
            const auto id = "lfo.step." + std::to_string(step + 1);
            stepValues[static_cast<std::size_t>(step)] = state.getRawParameterValue(id);
            stepParameters[static_cast<std::size_t>(step)] = state.getParameter(id);
        }

        startTimerHz(30);
    }

    ~LfoModulePanel() override
    {
        endStepGesture();
    }

    int preferredHeight(int) const override { return captionH + bodyHeight + 2 * padY; }

    void paint(juce::Graphics& g) override
    {
        paintPanelBody(g, getLocalBounds().toFloat().reduced(0.5f));
        auto header = getLocalBounds().removeFromTop(captionH);
        paintCaptionBar(g, header);
        paintModuleHeaderTick(g, header, zoneShape);

        auto titleArea = header.reduced(16, 0);
        const auto stepMode = isStepMode();
        if (stepMode)
        {
            auto badgeArea = titleArea.removeFromRight(52).withSizeKeepingCentre(52, 16);
            g.setColour(zoneShape.withAlpha(0.2f));
            g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
            g.setColour(zoneShape);
            g.setFont(uiFont(10.0f, true));
            g.drawText("STEP", badgeArea, juce::Justification::centred, false);
        }
        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("LFO", titleArea, juce::Justification::centred, true);

        paintDisplay(g);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(captionH);
        area = area.reduced(padX, padY);

        displayArea = area.removeFromRight(juce::jmax(170, area.getWidth() * 9 / 20)).reduced(2, 0);
        area.removeFromRight(8);

        // Compact control grid in the remaining left half.
        const auto columns = juce::jmax(2, (area.getWidth() + cellGap) / (cellWidth + cellGap));
        const auto columnWidth = (area.getWidth() - (columns - 1) * cellGap) / columns;
        int column = 0;
        int row = 0;
        for (const auto& control : controls)
        {
            const auto span = juce::jmin(control->isWide() ? 2 : 1, columns);
            if (column + span > columns)
            {
                column = 0;
                ++row;
            }
            control->setBounds(area.getX() + column * (columnWidth + cellGap),
                               area.getY() + row * (cellHeight + rowGap),
                               span * columnWidth + (span - 1) * cellGap, cellHeight);
            column += span;
        }
    }

    void mouseDown(const juce::MouseEvent& event) override { handleStepEdit(event, true); }
    void mouseDrag(const juce::MouseEvent& event) override { handleStepEdit(event, false); }
    void mouseUp(const juce::MouseEvent&) override { endStepGesture(); }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (!isStepMode() || !displayArea.contains(event.getPosition()))
            return;
        const auto step = stepIndexAt(event.position.x);
        if (auto* parameter = step >= 0 ? stepParameters[static_cast<std::size_t>(step)] : nullptr)
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
            parameter->endChangeGesture();
        }
    }

private:
    static constexpr int captionH = 26;
    static constexpr int bodyHeight = 150;
    static constexpr int padX = 11;
    static constexpr int padY = 7;
    static constexpr int cellWidth = 64;
    static constexpr int cellHeight = 70;
    static constexpr int cellGap = 6;
    static constexpr int rowGap = 4;

    bool isStepMode() const
    {
        return shapeValue != nullptr
            && static_cast<int>(std::round(shapeValue->load())) == static_cast<int>(synth::LfoShapeChoice::Step);
    }

    int activeStepCount() const
    {
        const auto raw = stepCountValue != nullptr ? stepCountValue->load() : 8.0f;
        return juce::jlimit(1, synth::lfoStepSlotCount, static_cast<int>(std::round(raw)));
    }

    int stepIndexAt(float x) const
    {
        const auto plot = displayArea.toFloat().reduced(1.0f);
        if (plot.getWidth() <= 0.0f)
            return -1;
        const auto fraction = (x - plot.getX()) / plot.getWidth();
        return juce::jlimit(0, activeStepCount() - 1,
                            static_cast<int>(fraction * static_cast<float>(activeStepCount())));
    }

    void handleStepEdit(const juce::MouseEvent& event, bool isDown)
    {
        if (!isStepMode())
            return;
        if (isDown && !displayArea.contains(event.getPosition()))
            return;

        const auto step = stepIndexAt(event.position.x);
        auto* parameter = step >= 0 ? stepParameters[static_cast<std::size_t>(step)] : nullptr;
        if (parameter == nullptr)
            return;

        if (gestureParameter != parameter)
        {
            endStepGesture();
            gestureParameter = parameter;
            gestureParameter->beginChangeGesture();
        }

        const auto plot = displayArea.toFloat().reduced(1.0f);
        const auto value = juce::jlimit(-1.0f, 1.0f,
                                        1.0f - 2.0f * (event.position.y - plot.getY()) / plot.getHeight());
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }

    void endStepGesture()
    {
        if (gestureParameter != nullptr)
        {
            gestureParameter->endChangeGesture();
            gestureParameter = nullptr;
        }
    }

    // Waveform preview for the classic shapes; mirrors the DSP shapes closely
    // enough to read correctly (S&H/Noise draw a deterministic held pattern).
    float shapeSample(int shape, float phase) const
    {
        phase -= std::floor(phase);
        switch (shape)
        {
            case 0: return std::sin(juce::MathConstants<float>::twoPi * phase);
            case 1: return 1.0f - 4.0f * std::abs(phase - 0.5f);
            case 2: return 2.0f * phase - 1.0f;
            case 3: return 1.0f - 2.0f * phase;
            case 4: return phase < 0.5f ? 1.0f : -1.0f;
            default:
            {
                // Held pseudo-random pattern over 8 segments, fixed seed.
                static constexpr std::array<float, 8> held {
                    0.62f, -0.35f, 0.91f, 0.12f, -0.78f, 0.44f, -0.96f, -0.08f
                };
                const auto segment = static_cast<std::size_t>(juce::jlimit(0, 7, static_cast<int>(phase * 8.0f)));
                return held[segment];
            }
        }
    }

    void paintDisplay(juce::Graphics& g)
    {
        const auto frame = displayArea.toFloat();
        g.setColour(fieldBg);
        g.fillRoundedRectangle(frame, 4.0f);

        const auto plot = frame.reduced(1.0f);
        const auto midY = plot.getCentreY();

        // Centre (zero) line.
        g.setColour(strokeSoft.withAlpha(0.9f));
        g.drawHorizontalLine(juce::roundToInt(midY), plot.getX() + 2.0f, plot.getRight() - 2.0f);

        const auto yFor = [&plot](float value) {
            return plot.getY() + (1.0f - juce::jlimit(-1.0f, 1.0f, value)) * 0.5f * plot.getHeight();
        };

        if (isStepMode())
        {
            const auto count = activeStepCount();
            const auto stepWidth = plot.getWidth() / static_cast<float>(count);
            for (int step = 0; step < count; ++step)
            {
                const auto* raw = stepValues[static_cast<std::size_t>(step)];
                const auto value = raw != nullptr ? juce::jlimit(-1.0f, 1.0f, raw->load()) : 0.0f;
                const auto x = plot.getX() + static_cast<float>(step) * stepWidth;
                const auto top = juce::jmin(midY, yFor(value));
                const auto bottom = juce::jmax(midY, yFor(value));
                auto bar = juce::Rectangle<float>(x + 1.0f, top, stepWidth - 2.0f,
                                                  juce::jmax(2.0f, bottom - top));
                g.setColour(zoneShape.withAlpha(0.30f));
                g.fillRect(bar);
                g.setColour(zoneShape);
                g.fillRect(juce::Rectangle<float>(bar.getX(), yFor(value) - 1.0f, bar.getWidth(), 2.0f));

                if (step > 0)
                {
                    g.setColour(strokeSoft.withAlpha(0.5f));
                    g.drawVerticalLine(juce::roundToInt(x), plot.getY() + 2.0f, plot.getBottom() - 2.0f);
                }
            }
        }
        else
        {
            const auto shape = shapeValue != nullptr ? static_cast<int>(std::round(shapeValue->load())) : 3;
            const auto phaseOffset = phaseValue != nullptr ? phaseValue->load() / 360.0f : 0.0f;
            juce::Path wave;
            constexpr int pointCount = 96;
            for (int i = 0; i <= pointCount; ++i)
            {
                const auto fraction = static_cast<float>(i) / static_cast<float>(pointCount);
                const auto x = plot.getX() + fraction * plot.getWidth();
                const auto y = yFor(shapeSample(shape, fraction + phaseOffset));
                if (i == 0)
                    wave.startNewSubPath(x, y);
                else
                    wave.lineTo(x, y);
            }
            g.setColour(zoneShape);
            g.strokePath(wave, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        // Live playhead + value dot from the audio thread's visual feed.
        if (lastVisual.lfoVoiceActive)
        {
            const auto x = plot.getX() + juce::jlimit(0.0f, 1.0f, lastVisual.lfoPhase) * plot.getWidth();
            g.setColour(text.withAlpha(0.55f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY() + 1.0f, plot.getBottom() - 1.0f);
            const auto y = yFor(lastVisual.lfoValue);
            g.setColour(text);
            g.fillEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }

        g.setColour(strokeSoft);
        g.drawRoundedRectangle(frame, 4.0f, 1.0f);
    }

    void timerCallback() override
    {
        if (!isShowing())
            return;
        lastVisual = audioProcessor.getUiVisualSnapshot();
        repaint(displayArea);
    }

    SynthAudioProcessor& audioProcessor;
    std::vector<std::unique_ptr<ParameterControl>> controls;
    std::atomic<float>* shapeValue = nullptr;
    std::atomic<float>* phaseValue = nullptr;
    std::atomic<float>* stepCountValue = nullptr;
    std::array<std::atomic<float>*, synth::lfoStepSlotCount> stepValues {};
    std::array<juce::RangedAudioParameter*, synth::lfoStepSlotCount> stepParameters {};
    juce::RangedAudioParameter* gestureParameter = nullptr;
    juce::Rectangle<int> displayArea;
    SynthAudioProcessor::UiVisualSnapshot lastVisual;
};

// ============================================================================
// ModSourcesPanel: the draggable modulation source strip.
//
// One chip per real TransMod source from the modulation catalog. Dragging a
// chip onto any halo-capable knob writes a route into a free TransMod slot
// (the drop handler lives on the editor). Chips show live route counts; the
// strip itself stores no routing state.
// ============================================================================
class SynthAudioProcessorEditor::ModSourcesPanel final : public LayoutSection
{
public:
    ModSourcesPanel()
    {
        for (const auto& source : synth::modulationSourceCatalog())
            if (source.source != synth::ModSource::None)
                chips.push_back({ &source, {}, 0 });
    }

    int preferredHeight(int width) const override
    {
        const auto rows = rowsForWidth(width);
        return captionH + padY * 2 + rows * chipHeight + (rows - 1) * chipGap;
    }

    void setRouteView(const synth::ModulationRouteView& view)
    {
        auto changed = false;
        for (auto& chip : chips)
        {
            int count = 0;
            for (const auto& route : view.activeRoutes)
            {
                if (route.source == chip.info->source)
                    ++count;
                if (route.scaler == chip.info->source && route.scaler != route.source)
                    ++count;
            }
            if (count != chip.routeCount)
            {
                chip.routeCount = count;
                changed = true;
            }
        }
        if (changed)
            repaint();
    }

    void paint(juce::Graphics& g) override
    {
        paintPanelBody(g, getLocalBounds().toFloat().reduced(0.5f));
        auto header = getLocalBounds().removeFromTop(captionH);
        paintCaptionBar(g, header);
        paintModuleHeaderTick(g, header, zoneMod);

        auto titleArea = header.reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(52).withSizeKeepingCentre(52, 16);
        g.setColour(zoneMod.withAlpha(0.2f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(zoneMod);
        g.setFont(uiFont(10.0f, true));
        g.drawText("DRAG", badgeArea, juce::Justification::centred, false);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("MOD SOURCES", titleArea.removeFromLeft(110), juce::Justification::centredLeft, true);
        g.setColour(mutedText);
        g.setFont(uiFont(10.5f));
        g.drawText("drag a chip onto a pitch / pulse / cutoff / level / pan knob to route it",
                   titleArea, juce::Justification::centredLeft, true);

        layoutChips();
        for (const auto& chip : chips)
        {
            const auto active = chip.routeCount > 0;
            g.setColour(active ? zoneMod.withAlpha(0.16f) : fieldBg);
            g.fillRoundedRectangle(chip.bounds.toFloat(), 5.0f);
            g.setColour(active ? zoneMod.withAlpha(0.8f) : strokeSoft);
            g.drawRoundedRectangle(chip.bounds.toFloat().reduced(0.5f), 5.0f, 1.0f);

            auto labelArea = chip.bounds.reduced(7, 0);
            // Grip dots signal draggability.
            g.setColour(mutedText.withAlpha(0.7f));
            const auto grip = labelArea.removeFromLeft(7);
            for (int dot = 0; dot < 3; ++dot)
                g.fillEllipse(static_cast<float>(grip.getX()),
                              static_cast<float>(grip.getCentreY() - 5 + dot * 4), 2.0f, 2.0f);
            labelArea.removeFromLeft(3);

            if (active)
            {
                auto countArea = labelArea.removeFromRight(20);
                g.setColour(zoneMod.withAlpha(0.25f));
                g.fillRoundedRectangle(countArea.toFloat().withSizeKeepingCentre(17.0f, 13.0f), 6.5f);
                g.setColour(zoneMod.brighter(0.2f));
                g.setFont(uiFont(9.5f, true));
                g.drawText(juce::String(chip.routeCount), countArea, juce::Justification::centred, false);
            }

            g.setColour(active ? text : mutedText);
            g.setFont(uiFont(10.5f, active));
            g.drawFittedText(juce::String(chip.info->label), labelArea,
                             juce::Justification::centredLeft, 1, 0.7f);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        pressedChip = -1;
        for (std::size_t i = 0; i < chips.size(); ++i)
        {
            if (chips[i].bounds.contains(event.getPosition()))
            {
                pressedChip = static_cast<int>(i);
                break;
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (pressedChip < 0 || event.getDistanceFromDragStart() < 5)
            return;

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        if (container == nullptr || container->isDragAndDropActive())
            return;

        const auto& chip = chips[static_cast<std::size_t>(pressedChip)];
        container->startDragging(juce::String(modSourceDragPrefix) + juce::String(chip.info->id),
                                 this, juce::ScaledImage(chipDragImage(chip)), false);
    }

    void mouseUp(const juce::MouseEvent&) override { pressedChip = -1; }

private:
    struct Chip
    {
        const synth::ModulationSourceInfo* info = nullptr;
        juce::Rectangle<int> bounds;
        int routeCount = 0;
    };

    static constexpr int captionH = 26;
    static constexpr int padX = 11;
    static constexpr int padY = 7;
    static constexpr int chipHeight = 22;
    static constexpr int chipGap = 6;
    static constexpr int minChipWidth = 104;

    int columnsForWidth(int width) const
    {
        const auto usable = std::max(1, width - padX * 2);
        return std::max(2, usable / (minChipWidth + chipGap));
    }

    int rowsForWidth(int width) const
    {
        const auto columns = columnsForWidth(width);
        return std::max(1, (static_cast<int>(chips.size()) + columns - 1) / columns);
    }

    void layoutChips()
    {
        auto area = getLocalBounds();
        area.removeFromTop(captionH);
        area = area.reduced(padX, padY);
        const auto columns = columnsForWidth(getWidth());
        const auto chipWidth = (area.getWidth() - (columns - 1) * chipGap) / columns;
        for (std::size_t i = 0; i < chips.size(); ++i)
        {
            const auto column = static_cast<int>(i) % columns;
            const auto row = static_cast<int>(i) / columns;
            chips[i].bounds = { area.getX() + column * (chipWidth + chipGap),
                                area.getY() + row * (chipHeight + chipGap),
                                chipWidth, chipHeight };
        }
    }

    juce::Image chipDragImage(const Chip& chip) const
    {
        const auto width = juce::jmax(60, chip.bounds.getWidth());
        juce::Image image(juce::Image::ARGB, width, chipHeight, true);
        juce::Graphics g(image);
        g.setColour(zoneMod.withAlpha(0.9f));
        g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                      static_cast<float>(chipHeight)), 5.0f);
        g.setColour(onAccentText);
        g.setFont(uiFont(10.5f, true));
        g.drawText(juce::String(chip.info->label), 0, 0, width, chipHeight,
                   juce::Justification::centred, true);
        return image;
    }

    std::vector<Chip> chips;
    int pressedChip = -1;
};

// ============================================================================
// PresetBrowserPanel: searchable preset list over the real preset catalog.
// ============================================================================
class SynthAudioProcessorEditor::PresetBrowserPanel final : public LayoutSection,
                                                           private juce::ListBoxModel
{
public:
    explicit PresetBrowserPanel(SynthAudioProcessorEditor& editor)
        : owner(editor)
    {
        searchEditor.setTextToShowWhenEmpty("Search", mutedText);
        searchEditor.setFont(uiFont(12.0f));
        searchEditor.onTextChange = [this] { applyFilters(); };
        addAndMakeVisible(searchEditor);

        categoryBox.setTextWhenNothingSelected("All Categories");
        categoryBox.onChange = [this] {
            if (rebuildingCategoryBox)
                return;

            selectedCategory = categoryBox.getSelectedId() <= 1 ? juce::String{} : categoryBox.getText();
            applyFilters();
        };
        addAndMakeVisible(categoryBox);

        configureFilterButton(factoryButton, "Factory", true);
        configureFilterButton(userButton, "User", true);
        configureFilterButton(legacyButton, "Legacy", true);
        configureFilterButton(favoritesButton, "Fav", false);
        refreshFilterButtons();

        presetList.setModel(this);
        presetList.setRowHeight(rowHeight);
        presetList.setMultipleSelectionEnabled(false);
        presetList.setColour(juce::ListBox::backgroundColourId, fieldBg);
        presetList.setOutlineThickness(0);
        addAndMakeVisible(presetList);
    }

    int preferredHeight(int) const override
    {
        return 226;
    }

    void setItems(std::vector<SynthAudioProcessor::PresetListItem> items,
                  const juce::String& activePresetPath,
                  const juce::String& activePresetName)
    {
        allItems = std::move(items);
        currentPresetPath = activePresetPath;
        currentPresetName = activePresetName;
        rebuildCategoryFilter();
        applyFilters();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneUtility);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto countArea = titleArea.removeFromRight(88).withSizeKeepingCentre(88, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("PRESET BROWSER", titleArea, juce::Justification::centredLeft, true);

        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(countArea.toFloat(), 8.0f);
        g.setColour(accent);
        g.setFont(uiFont(10.0f, true));
        g.drawText(juce::String(filteredItemIndices.size()) + " / " + juce::String(allItems.size()),
                   countArea, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(padX, padY);

        auto filters = content.removeFromTop(filterHeight);
        searchEditor.setBounds(filters.removeFromLeft(260));
        filters.removeFromLeft(8);
        categoryBox.setBounds(filters.removeFromLeft(176));
        filters.removeFromLeft(12);

        const auto placeButton = [&filters](juce::TextButton& button, int width) {
            button.setBounds(filters.removeFromLeft(width));
            filters.removeFromLeft(6);
        };

        placeButton(factoryButton, 74);
        placeButton(userButton, 58);
        placeButton(legacyButton, 64);
        placeButton(favoritesButton, 52);

        content.removeFromTop(8);
        presetList.setBounds(content);
    }

private:
    static constexpr int headerHeight = 26;
    static constexpr int padX = 12;
    static constexpr int padY = 8;
    static constexpr int filterHeight = 28;
    static constexpr int rowHeight = 28;
    static constexpr int favoriteHitWidth = 34;

    void configureFilterButton(juce::TextButton& button, const juce::String& label, bool active)
    {
        button.setButtonText(label);
        button.setClickingTogglesState(true);
        button.setToggleState(active, juce::dontSendNotification);
        button.onClick = [this] {
            refreshFilterButtons();
            applyFilters();
        };
        addAndMakeVisible(button);
    }

    void refreshFilterButtons()
    {
        auto refreshButton = [](juce::TextButton& button) {
            const auto active = button.getToggleState();
            styleFlatButton(button,
                            active ? accent.darker(0.18f) : fieldBg,
                            active ? onAccentText : mutedText);
        };

        refreshButton(factoryButton);
        refreshButton(userButton);
        refreshButton(legacyButton);
        refreshButton(favoritesButton);
    }

    void rebuildCategoryFilter()
    {
        juce::StringArray categories;
        for (const auto& item : allItems)
        {
            const auto category = item.category.trim();
            if (category.isNotEmpty() && !categories.contains(category, true))
                categories.add(category);
        }
        categories.sort(true);

        const auto previousCategory = selectedCategory;
        rebuildingCategoryBox = true;
        categoryBox.clear(juce::dontSendNotification);
        categoryBox.addItem("All Categories", 1);

        selectedCategory = {};
        int selectedId = 1;
        for (int index = 0; index < categories.size(); ++index)
        {
            const auto id = index + 2;
            categoryBox.addItem(categories[index], id);
            if (categories[index] == previousCategory)
            {
                selectedCategory = previousCategory;
                selectedId = id;
            }
        }

        categoryBox.setSelectedId(selectedId, juce::dontSendNotification);
        rebuildingCategoryBox = false;
    }

    bool sourceIncluded(const SynthAudioProcessor::PresetListItem& item) const
    {
        const auto source = item.sourceLabel.trim().toLowerCase();
        if (item.factory)
            return factoryButton.getToggleState();
        if (source == "legacy user")
            return legacyButton.getToggleState();
        return userButton.getToggleState();
    }

    bool matchesSearch(const SynthAudioProcessor::PresetListItem& item, const juce::String& query) const
    {
        if (query.isEmpty())
            return true;

        auto containsQuery = [&query](const juce::String& value) {
            return value.toLowerCase().contains(query);
        };

        if (containsQuery(item.displayName) || containsQuery(item.author) || containsQuery(item.description)
            || containsQuery(item.bank) || containsQuery(item.category) || containsQuery(item.sourceLabel)
            || containsQuery(item.validationMessage)
            || containsQuery(item.file.getFileNameWithoutExtension()))
            return true;

        for (const auto& tag : item.tags)
        {
            if (containsQuery(tag))
                return true;
        }

        return false;
    }

    void applyFilters()
    {
        filteredItemIndices.clear();
        const auto query = searchEditor.getText().trim().toLowerCase();

        for (int index = 0; index < static_cast<int>(allItems.size()); ++index)
        {
            const auto& item = allItems[static_cast<std::size_t>(index)];
            if (!sourceIncluded(item))
                continue;
            if (favoritesButton.getToggleState() && !item.favorite)
                continue;
            if (selectedCategory.isNotEmpty() && item.category != selectedCategory)
                continue;
            if (!matchesSearch(item, query))
                continue;

            filteredItemIndices.push_back(index);
        }

        presetList.updateContent();
        const auto activeRow = activeFilteredRow();
        if (activeRow >= 0)
            presetList.selectRow(activeRow, false, true);
        else
            presetList.deselectAllRows();
        presetList.repaint();
    }

    int activeFilteredRow() const
    {
        for (int row = 0; row < static_cast<int>(filteredItemIndices.size()); ++row)
        {
            const auto itemIndex = filteredItemIndices[static_cast<std::size_t>(row)];
            const auto& item = allItems[static_cast<std::size_t>(itemIndex)];
            if (isCurrentPreset(item))
                return row;
        }
        return -1;
    }

    int getNumRows() override
    {
        return static_cast<int>(filteredItemIndices.size());
    }

    void paintListBoxItem(int rowNumber,
                          juce::Graphics& g,
                          int width,
                          int height,
                          bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= getNumRows())
            return;

        const auto itemIndex = filteredItemIndices[static_cast<std::size_t>(rowNumber)];
        const auto& item = allItems[static_cast<std::size_t>(itemIndex)];
        const auto active = isCurrentPreset(item);

        auto row = juce::Rectangle<int>(0, 0, width, height).reduced(1, 1);
        g.setColour(!item.valid ? warn.withAlpha(rowIsSelected ? 0.22f : 0.12f)
                                : (active ? accent.withAlpha(0.18f)
                                          : (rowIsSelected ? strokeSoft.brighter(0.18f) : fieldBg)));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);

        auto favoriteArea = row.removeFromLeft(favoriteHitWidth);
        const auto starBounds = favoriteArea.withSizeKeepingCentre(20, 18).toFloat();
        g.setColour(!item.valid ? warn.withAlpha(0.24f)
                                : (item.favorite ? staged.withAlpha(0.22f) : strokeSoft.withAlpha(0.55f)));
        g.fillRoundedRectangle(starBounds, 5.0f);
        g.setColour(!item.valid ? warn : (item.favorite ? staged : mutedText));
        g.setFont(uiFont(13.0f, false));
        g.drawText(!item.valid ? "!" : juce::String::fromUTF8(item.favorite ? "\xe2\x98\x85" : "\xe2\x98\x86"),
                   favoriteArea, juce::Justification::centred, false);

        row.removeFromLeft(4);
        auto sourceArea = row.removeFromRight(92);
        row.removeFromRight(8);
        auto metaArea = row.removeFromRight(218);
        row.removeFromRight(8);

        const auto displayName = item.displayName.isNotEmpty()
            ? item.displayName
            : item.file.getFileNameWithoutExtension();

        g.setColour(!item.valid ? warn.brighter(0.18f)
                                : (active ? text : text));
        g.setFont(uiFont(12.0f, active));
        g.drawFittedText(displayName, row, juce::Justification::centredLeft, 1, 0.68f);

        auto meta = item.valid ? item.bank : item.validationMessage;
        if (item.valid && item.category.isNotEmpty())
            meta << " / " << item.category;
        if (item.valid && !item.tags.isEmpty())
        {
            juce::StringArray visibleTags;
            for (int index = 0; index < juce::jmin(2, item.tags.size()); ++index)
                visibleTags.add(item.tags[index]);
            meta << " / " << visibleTags.joinIntoString(", ");
        }

        g.setColour(item.valid ? mutedText : warn);
        g.setFont(uiFont(10.5f));
        g.drawFittedText(meta, metaArea, juce::Justification::centredRight, 1, 0.58f);

        g.setColour(!item.valid ? warn : (item.factory ? live : (item.sourceLabel == "Legacy User" ? staged : accent)));
        g.setFont(uiFont(10.0f, true));
        g.drawFittedText(item.valid ? item.sourceLabel : "INVALID",
                         sourceArea, juce::Justification::centredRight, 1, 0.62f);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto itemIndex = filteredItemIndices[static_cast<std::size_t>(row)];
        const auto& item = allItems[static_cast<std::size_t>(itemIndex)];
        if (!item.valid)
        {
            owner.updateStatus("Invalid preset: " + item.validationMessage);
            return;
        }

        if (event.x < favoriteHitWidth)
            owner.togglePresetFavoriteAtIndex(itemIndex);
        else
            owner.loadPresetAtIndex(itemIndex);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& event) override
    {
        if (event.x < favoriteHitWidth)
            return;

        if (row >= 0 && row < getNumRows())
        {
            const auto itemIndex = filteredItemIndices[static_cast<std::size_t>(row)];
            const auto& item = allItems[static_cast<std::size_t>(itemIndex)];
            if (!item.valid)
            {
                owner.updateStatus("Invalid preset: " + item.validationMessage);
                return;
            }

            owner.loadPresetAtIndex(itemIndex);
        }
    }

    bool isCurrentPreset(const SynthAudioProcessor::PresetListItem& item) const
    {
        if (!item.valid)
            return false;

        if (currentPresetPath.isNotEmpty())
            return item.file.getFullPathName() == currentPresetPath;

        return currentPresetName.isNotEmpty() && item.displayName == currentPresetName;
    }

    SynthAudioProcessorEditor& owner;
    juce::TextEditor searchEditor;
    juce::ComboBox categoryBox;
    juce::TextButton factoryButton;
    juce::TextButton userButton;
    juce::TextButton legacyButton;
    juce::TextButton favoritesButton;
    juce::ListBox presetList;
    std::vector<SynthAudioProcessor::PresetListItem> allItems;
    std::vector<int> filteredItemIndices;
    juce::String currentPresetPath;
    juce::String currentPresetName;
    juce::String selectedCategory;
    bool rebuildingCategoryBox = false;
};

// ============================================================================
// PresetWorkflowPanel: real preset command and A/B compare controls.
// ============================================================================
class SynthAudioProcessorEditor::PresetWorkflowPanel final : public LayoutSection
{
public:
    explicit PresetWorkflowPanel(SynthAudioProcessorEditor& editor)
        : owner(editor)
    {
        configureCommandButton(initButton, "Init", [this] { owner.initializePreset(); });
        configureCommandButton(randomButton, "Random", [this] { owner.randomizePreset(); });
        configureCommandButton(resetButton, "Reset", [this] { owner.resetPreset(); });
        configureCommandButton(storeAButton, "A Store", [this] { owner.captureCompareSlot(0); });
        configureCommandButton(loadAButton, "A Load", [this] { owner.recallCompareSlot(0); });
        configureCommandButton(storeBButton, "B Store", [this] { owner.captureCompareSlot(1); });
        configureCommandButton(loadBButton, "B Load", [this] { owner.recallCompareSlot(1); });

        statusPill.setJustificationType(juce::Justification::centred);
        statusPill.setFont(uiFont(11.0f, true));
        addAndMakeVisible(statusPill);
    }

    int preferredHeight(int) const override
    {
        return 94;
    }

    void refresh(const SynthAudioProcessor::PresetWorkflowSnapshot& snapshot)
    {
        const auto dirtyText = snapshot.dirty ? "EDITED" : "CLEAN";
        const auto detail = snapshot.currentPreset.isNotEmpty() ? snapshot.currentPreset : "Init";
        const auto textToShow = dirtyText + juce::String(" / ") + detail;
        if (statusPill.getText() != textToShow)
            statusPill.setText(textToShow, juce::dontSendNotification);

        statusPill.setColour(juce::Label::backgroundColourId,
                             (snapshot.dirty ? staged : live).withAlpha(0.18f));
        statusPill.setColour(juce::Label::textColourId, snapshot.dirty ? staged : live);
        resetButton.setEnabled(snapshot.resetAvailable);
        loadAButton.setEnabled(snapshot.compareSlotAReady);
        loadBButton.setEnabled(snapshot.compareSlotBReady);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneUtility);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(82).withSizeKeepingCentre(82, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("PRESET WORKFLOW", titleArea, juce::Justification::centredLeft, true);

        g.setColour(info.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(info);
        g.setFont(uiFont(10.0f, true));
        g.drawText("A/B READY", badgeArea, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(12, 8);

        const auto compact = content.getWidth() < 760;
        auto row = content.removeFromTop(28);
        statusPill.setBounds(row.removeFromLeft(compact ? 150 : 210));
        row.removeFromLeft(8);

        const auto commandWidth = compact ? 62 : 74;
        placeButton(row, initButton, commandWidth);
        placeButton(row, randomButton, compact ? 74 : 86);
        placeButton(row, resetButton, commandWidth);

        row.removeFromLeft(8);
        placeButton(row, storeAButton, compact ? 72 : 82);
        placeButton(row, loadAButton, compact ? 68 : 78);
        row.removeFromLeft(4);
        placeButton(row, storeBButton, compact ? 72 : 82);
        placeButton(row, loadBButton, compact ? 68 : 78);
    }

private:
    static constexpr int headerHeight = 26;

    void configureCommandButton(juce::TextButton& button, const juce::String& label, std::function<void()> handler)
    {
        button.setButtonText(label);
        styleFlatButton(button, buttonBg, text);
        button.onClick = std::move(handler);
        addAndMakeVisible(button);
    }

    static void placeButton(juce::Rectangle<int>& row, juce::TextButton& button, int width)
    {
        button.setBounds(row.removeFromLeft(width));
        row.removeFromLeft(6);
    }

    SynthAudioProcessorEditor& owner;
    juce::Label statusPill;
    juce::TextButton initButton;
    juce::TextButton randomButton;
    juce::TextButton resetButton;
    juce::TextButton storeAButton;
    juce::TextButton loadAButton;
    juce::TextButton storeBButton;
    juce::TextButton loadBButton;
};

// ============================================================================
// PresetMetadataPanel: metadata-aware safe-save controls over the real writer.
// ============================================================================
class SynthAudioProcessorEditor::PresetMetadataPanel final : public LayoutSection
{
public:
    explicit PresetMetadataPanel(SynthAudioProcessorEditor& editor)
        : owner(editor)
    {
        configureEditor(displayNameEditor, "Preset name");
        configureEditor(authorEditor, "Author");
        configureEditor(bankEditor, "Bank");
        configureEditor(categoryEditor, "Category");
        configureEditor(tagsEditor, "Tags, comma separated");
        configureEditor(descriptionEditor, "Notes");
        descriptionEditor.setMultiLine(true, true);
        descriptionEditor.setReturnKeyStartsNewLine(true);

        configureCommandButton(saveNewButton, "Save New", [this] {
            owner.savePresetWithMetadata(synth::PresetWriteMode::CreateNew);
        });
        configureCommandButton(overwriteButton, "Overwrite", [this] {
            owner.savePresetWithMetadata(synth::PresetWriteMode::OverwriteExisting);
        });
    }

    int preferredHeight(int width) const override
    {
        return width < 760 ? 184 : 132;
    }

    void syncMetadata(const SynthAudioProcessor::PresetListItem* item,
                      const juce::String& fallbackPresetName)
    {
        const auto name = item != nullptr ? item->displayName : fallbackPresetName;
        setEditorText(displayNameEditor, nonEmptyField(name, "Init"));
        setEditorText(authorEditor, item != nullptr ? item->author : "User");
        setEditorText(bankEditor, item != nullptr ? item->bank : "User");
        setEditorText(categoryEditor, item != nullptr ? item->category : "User");
        setEditorText(tagsEditor, item != nullptr ? item->tags.joinIntoString(", ") : "user");
        setEditorText(descriptionEditor, item != nullptr ? item->description
                                                         : "User preset saved from the Synthia editor.");
    }

    synth::PresetWriteOptions writeOptions(synth::PresetWriteMode mode) const
    {
        synth::PresetWriteOptions options;
        options.mode = mode;
        options.metadata.displayName = displayNameEditor.getText().trim().toStdString();
        options.metadata.author = authorEditor.getText().trim().toStdString();
        options.metadata.description = descriptionEditor.getText().trim().toStdString();
        options.metadata.bank = bankEditor.getText().trim().toStdString();
        options.metadata.category = categoryEditor.getText().trim().toStdString();
        options.metadata.tags = parseTags(tagsEditor.getText());
        return options;
    }

    juce::String presetName() const
    {
        return displayNameEditor.getText().trim();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneUtility);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(92).withSizeKeepingCentre(92, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("PRESET SAVE", titleArea, juce::Justification::centredLeft, true);

        g.setColour(live.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(live);
        g.setFont(uiFont(10.0f, true));
        g.drawText("NO-CLOBBER", badgeArea, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(12, 8);

        if (content.getWidth() < 760)
        {
            auto row = content.removeFromTop(28);
            displayNameEditor.setBounds(row.removeFromLeft(220));
            row.removeFromLeft(8);
            authorEditor.setBounds(row);

            content.removeFromTop(7);
            row = content.removeFromTop(28);
            bankEditor.setBounds(row.removeFromLeft(160));
            row.removeFromLeft(8);
            categoryEditor.setBounds(row.removeFromLeft(160));
            row.removeFromLeft(8);
            tagsEditor.setBounds(row);

            content.removeFromTop(7);
            row = content.removeFromTop(52);
            descriptionEditor.setBounds(row);

            content.removeFromTop(8);
            row = content.removeFromTop(28);
            saveNewButton.setBounds(row.removeFromLeft(92));
            row.removeFromLeft(8);
            overwriteButton.setBounds(row.removeFromLeft(96));
            return;
        }

        auto topRow = content.removeFromTop(28);
        displayNameEditor.setBounds(topRow.removeFromLeft(220));
        topRow.removeFromLeft(8);
        authorEditor.setBounds(topRow.removeFromLeft(148));
        topRow.removeFromLeft(8);
        bankEditor.setBounds(topRow.removeFromLeft(136));
        topRow.removeFromLeft(8);
        categoryEditor.setBounds(topRow.removeFromLeft(136));
        topRow.removeFromLeft(8);
        tagsEditor.setBounds(topRow);

        content.removeFromTop(8);
        auto bottomRow = content.removeFromTop(48);
        auto buttons = bottomRow.removeFromRight(202);
        buttons.removeFromLeft(8);
        saveNewButton.setBounds(buttons.removeFromLeft(94).withSizeKeepingCentre(94, 28));
        buttons.removeFromLeft(8);
        overwriteButton.setBounds(buttons.removeFromLeft(92).withSizeKeepingCentre(92, 28));
        descriptionEditor.setBounds(bottomRow);
    }

private:
    static constexpr int headerHeight = 26;

    static std::vector<std::string> parseTags(const juce::String& rawTags)
    {
        juce::StringArray tokens;
        tokens.addTokens(rawTags, ",", "");
        tokens.trim();
        tokens.removeEmptyStrings();

        std::vector<std::string> tags;
        for (const auto& token : tokens)
            tags.push_back(token.toStdString());
        return tags;
    }

    static juce::String nonEmptyField(const juce::String& value, const juce::String& fallback)
    {
        const auto trimmed = value.trim();
        return trimmed.isNotEmpty() ? trimmed : fallback;
    }

    static void setEditorText(juce::TextEditor& editor, const juce::String& textToShow)
    {
        if (editor.hasKeyboardFocus(false))
            return;

        if (editor.getText() != textToShow)
            editor.setText(textToShow, juce::dontSendNotification);
    }

    void configureEditor(juce::TextEditor& editor, const juce::String& placeholder)
    {
        styleTextEditor(editor);
        editor.setTextToShowWhenEmpty(placeholder, mutedText);
        addAndMakeVisible(editor);
    }

    void configureCommandButton(juce::TextButton& button, const juce::String& label, std::function<void()> handler)
    {
        button.setButtonText(label);
        styleFlatButton(button, label == "Overwrite" ? warn.darker(0.2f) : accent.darker(0.18f),
                        label == "Overwrite" ? text : onAccentText);
        button.onClick = std::move(handler);
        addAndMakeVisible(button);
    }

    SynthAudioProcessorEditor& owner;
    juce::TextEditor displayNameEditor;
    juce::TextEditor authorEditor;
    juce::TextEditor bankEditor;
    juce::TextEditor categoryEditor;
    juce::TextEditor tagsEditor;
    juce::TextEditor descriptionEditor;
    juce::TextButton saveNewButton;
    juce::TextButton overwriteButton;
};

// ============================================================================
// MidiControllerPanel: global MIDI learn/forget surface for automatable params.
// ============================================================================
class SynthAudioProcessorEditor::MidiControllerPanel final : public LayoutSection
{
public:
    explicit MidiControllerPanel(SynthAudioProcessorEditor& editor)
        : owner(editor)
    {
        parameterBox.setTextWhenNothingSelected("Select parameter");
        int itemId = 1;
        for (const auto& spec : synth::getParameterSpecs())
        {
            if (!spec.automatable)
                continue;

            parameterIds.push_back(spec.id);
            parameterBox.addItem(juce::String(spec.group).toUpperCase() + " / " + spec.name, itemId++);
        }
        addAndMakeVisible(parameterBox);

        styleFlatButton(learnButton, accent.darker(0.18f), onAccentText);
        styleFlatButton(forgetButton, buttonBg, text);
        styleFlatButton(cancelButton, buttonBg, mutedText);
        addAndMakeVisible(learnButton);
        addAndMakeVisible(forgetButton);
        addAndMakeVisible(cancelButton);

        learnButton.onClick = [this] {
            juce::String message;
            if (owner.audioProcessor.startMidiLearn(selectedParameterId(), message))
                refresh();
            owner.updateStatus(message);
        };
        forgetButton.onClick = [this] {
            juce::String message;
            owner.audioProcessor.forgetMidiControllerForParameter(selectedParameterId(), message);
            refresh();
            owner.updateStatus(message);
        };
        cancelButton.onClick = [this] {
            owner.audioProcessor.cancelMidiLearn();
            refresh();
            owner.updateStatus("MIDI learn canceled");
        };

        statusLabel.setColour(juce::Label::textColourId, mutedText);
        statusLabel.setFont(uiFont(11.0f));
        statusLabel.setMinimumHorizontalScale(0.65f);
        addAndMakeVisible(statusLabel);

        assignmentLabel.setColour(juce::Label::textColourId, text);
        assignmentLabel.setFont(uiFont(11.0f));
        assignmentLabel.setMinimumHorizontalScale(0.55f);
        addAndMakeVisible(assignmentLabel);

        refresh();
    }

    int preferredHeight(int width) const override
    {
        return width < 760 ? 142 : 112;
    }

    void refresh()
    {
        statusLabel.setText(owner.audioProcessor.getMidiControllerStatus(), juce::dontSendNotification);

        const auto assignments = owner.audioProcessor.getMidiControllerAssignments();
        juce::StringArray visible;
        for (int index = 0; index < juce::jmin(4, static_cast<int>(assignments.size())); ++index)
        {
            const auto& assignment = assignments[static_cast<std::size_t>(index)];
            visible.add("CC" + juce::String(assignment.controllerNumber) + " -> "
                        + juce::String(assignment.parameterId));
        }

        const auto hiddenAssignmentCount = static_cast<int>(assignments.size()) - visible.size();
        if (hiddenAssignmentCount > 0)
            visible.add("+" + juce::String(hiddenAssignmentCount) + " more");

        assignmentLabel.setText(visible.isEmpty() ? "No MIDI CC mappings" : visible.joinIntoString("   "),
                                juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneUtility);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(70).withSizeKeepingCentre(70, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("MIDI CONTROL", titleArea, juce::Justification::centredLeft, true);

        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(accent);
        g.setFont(uiFont(10.0f, true));
        g.drawText("GLOBAL", badgeArea, juce::Justification::centred, false);
    }

    void resized() override
    {
        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(12, 8);

        if (content.getWidth() < 760)
        {
            parameterBox.setBounds(content.removeFromTop(28));
            content.removeFromTop(6);

            auto buttons = content.removeFromTop(28);
            learnButton.setBounds(buttons.removeFromLeft(70));
            buttons.removeFromLeft(6);
            forgetButton.setBounds(buttons.removeFromLeft(72));
            buttons.removeFromLeft(6);
            cancelButton.setBounds(buttons.removeFromLeft(70));
        }
        else
        {
            auto controls = content.removeFromTop(28);
            parameterBox.setBounds(controls.removeFromLeft(360));
            controls.removeFromLeft(8);
            learnButton.setBounds(controls.removeFromLeft(70));
            controls.removeFromLeft(6);
            forgetButton.setBounds(controls.removeFromLeft(72));
            controls.removeFromLeft(6);
            cancelButton.setBounds(controls.removeFromLeft(70));
        }

        content.removeFromTop(5);
        statusLabel.setBounds(content.removeFromTop(18));
        assignmentLabel.setBounds(content.removeFromTop(18));
    }

private:
    juce::String selectedParameterId() const
    {
        const auto selectedIndex = parameterBox.getSelectedId() - 1;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(parameterIds.size()))
            return {};

        return juce::String(parameterIds[static_cast<std::size_t>(selectedIndex)]);
    }

    static constexpr int headerHeight = 26;

    SynthAudioProcessorEditor& owner;
    juce::ComboBox parameterBox;
    juce::TextButton learnButton { "Learn" };
    juce::TextButton forgetButton { "Forget" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;
    juce::Label assignmentLabel;
    std::vector<std::string> parameterIds;
};

// ============================================================================
// ModulationOverviewPanel: read-only source and route inspection.
// ============================================================================
class SynthAudioProcessorEditor::ModulationOverviewPanel final : public LayoutSection
{
public:
    explicit ModulationOverviewPanel(SynthAudioProcessor& processor)
        : audioProcessor(processor)
    {
        refresh();
    }

    int preferredHeight(int width) const override
    {
        return headerHeight + padY * 2 + labelHeight * 2 + sectionGap
               + sourceRowsForWidth(width) * chipHeight
               + std::max(0, sourceRowsForWidth(width) - 1) * chipGap
               + routeRows * routeHeight + (routeRows - 1) * routeGap;
    }

    void refresh()
    {
        routeView = audioProcessor.getModulationRouteView();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneMod);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(52).withSizeKeepingCentre(52, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("MODULATION OVERVIEW", titleArea, juce::Justification::centredLeft, true);

        g.setColour(live.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(live);
        g.setFont(uiFont(10.0f, true));
        g.drawText("READ", badgeArea, juce::Justification::centred, false);

        auto content = getLocalBounds();
        content.removeFromTop(headerHeight);
        content = content.reduced(padX, padY);

        auto sourceLabelArea = content.removeFromTop(labelHeight);
        paintSectionLabel(g, sourceLabelArea, "SOURCES");
        const auto sourceRows = sourceRowsForWidth(getWidth());
        auto sourceArea = content.removeFromTop(sourceRows * chipHeight + std::max(0, sourceRows - 1) * chipGap);
        paintSources(g, sourceArea);

        content.removeFromTop(sectionGap);
        auto routeLabelArea = content.removeFromTop(labelHeight);
        const auto activeRouteCount = static_cast<int>(routeView.activeRoutes.size());
        paintSectionLabel(g, routeLabelArea,
                          activeRouteCount > 0 ? "ACTIVE ROUTES  (" + juce::String(activeRouteCount) + ")"
                                               : "ACTIVE ROUTES");
        paintRoutes(g, content.removeFromTop(routeRows * routeHeight + (routeRows - 1) * routeGap));
    }

private:
    static constexpr int headerHeight = 26;
    static constexpr int padX = 12;
    static constexpr int padY = 8;
    static constexpr int labelHeight = 15;
    static constexpr int sectionGap = 9;
    static constexpr int chipHeight = 22;
    static constexpr int chipGap = 6;
    static constexpr int routeHeight = 24;
    static constexpr int routeGap = 6;
    static constexpr int routeRows = 2;
    static constexpr int minChipWidth = 92;
    static constexpr int minRouteWidth = 226;
    static constexpr std::size_t sourceCount = static_cast<std::size_t>(synth::ModSource::Macro4) + 1;

    int sourceColumnsForWidth(int width) const
    {
        const auto usableWidth = std::max(1, width - padX * 2);
        return std::max(2, usableWidth / (minChipWidth + chipGap));
    }

    int sourceRowsForWidth(int width) const
    {
        int visibleSources = 0;
        for (const auto& source : synth::modulationSourceCatalog())
        {
            if (source.source != synth::ModSource::None)
                ++visibleSources;
        }

        const auto columns = sourceColumnsForWidth(width);
        return std::max(1, (visibleSources + columns - 1) / columns);
    }

    int routeColumnsForWidth(int width) const
    {
        const auto usableWidth = std::max(1, width - padX * 2);
        return std::max(1, usableWidth / (minRouteWidth + routeGap));
    }

    void incrementSourceUseCount(std::array<int, sourceCount>& counts, synth::ModSource source) const
    {
        const auto sourceIndex = static_cast<int>(source);
        if (sourceIndex >= 0 && sourceIndex < static_cast<int>(counts.size()))
            ++counts[static_cast<std::size_t>(sourceIndex)];
    }

    std::array<int, sourceCount> sourceUseCounts() const
    {
        std::array<int, sourceCount> counts {};
        for (const auto& route : routeView.activeRoutes)
        {
            incrementSourceUseCount(counts, route.source);
            if (route.scaler != synth::ModSource::None && route.scaler != route.source)
                incrementSourceUseCount(counts, route.scaler);
        }
        return counts;
    }

    void paintSectionLabel(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label) const
    {
        g.setColour(mutedText);
        g.setFont(uiFont(10.0f, true));
        g.drawText(label, area, juce::Justification::centredLeft, true);
    }

    void paintSources(juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto counts = sourceUseCounts();
        const auto columns = sourceColumnsForWidth(getWidth());
        const auto chipWidth = (area.getWidth() - (columns - 1) * chipGap) / columns;

        int visibleIndex = 0;
        for (const auto& source : synth::modulationSourceCatalog())
        {
            if (source.source == synth::ModSource::None)
                continue;

            const auto column = visibleIndex % columns;
            const auto row = visibleIndex / columns;
            auto chip = juce::Rectangle<int>(
                area.getX() + column * (chipWidth + chipGap),
                area.getY() + row * (chipHeight + chipGap),
                chipWidth,
                chipHeight);

            const auto sourceIndex = static_cast<int>(source.source);
            const auto routeCount = sourceIndex >= 0 && sourceIndex < static_cast<int>(counts.size())
                ? counts[static_cast<std::size_t>(sourceIndex)]
                : 0;

            const auto activeFill = routeCount > 0 ? accent.withAlpha(0.16f) : fieldBg;
            const auto outline = routeCount > 0 ? accent.withAlpha(0.72f) : strokeSoft;
            g.setColour(activeFill);
            g.fillRoundedRectangle(chip.toFloat(), 5.0f);
            g.setColour(outline);
            g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 5.0f, 1.0f);

            auto labelArea = chip.reduced(7, 0);
            if (routeCount > 0)
            {
                auto countArea = labelArea.removeFromRight(22);
                g.setColour(accent.withAlpha(0.22f));
                g.fillRoundedRectangle(countArea.toFloat().withSizeKeepingCentre(18.0f, 14.0f), 7.0f);
                g.setColour(accent);
                g.setFont(uiFont(10.0f, true));
                g.drawText(juce::String(routeCount), countArea, juce::Justification::centred, false);
                labelArea.removeFromRight(3);
            }

            g.setColour(routeCount > 0 ? text : mutedText);
            g.setFont(uiFont(10.5f, routeCount > 0));
            g.drawFittedText(juce::String(source.label), labelArea, juce::Justification::centredLeft, 1, 0.72f);

            ++visibleIndex;
        }
    }

    juce::String sourceLabel(synth::ModSource source) const
    {
        if (const auto* sourceInfo = synth::findModulationSourceInfo(source))
            return juce::String(sourceInfo->label);
        return "None";
    }

    juce::String destinationLabel(synth::ModulationDestination destination) const
    {
        if (const auto* destinationInfo = synth::findModulationDestinationInfo(destination))
            return juce::String(destinationInfo->label);
        return "Target";
    }

    juce::String signedNumber(float value, int decimals) const
    {
        return juce::String(value > 0.0f ? "+" : "") + juce::String(value, decimals);
    }

    juce::String depthLabel(const synth::ModulationRouteSummary& route) const
    {
        const auto* destination = synth::findModulationDestinationInfo(route.destination);
        if (destination == nullptr)
            return signedNumber(route.depth, 2);

        if (destination->unit == "semitones")
            return signedNumber(route.depth, 1) + " st";
        if (destination->unit == "dB")
            return signedNumber(route.depth, 1) + " dB";
        if (destination->unit == "normalized" || destination->unit == "percent")
            return signedNumber(route.depth * 100.0f, 0) + "%";

        return signedNumber(route.depth, 2);
    }

    juce::String routeLabel(const synth::ModulationRouteSummary& route) const
    {
        auto label = sourceLabel(route.source) + " -> " + destinationLabel(route.destination);
        if (route.scaler != synth::ModSource::None)
            label += " x " + sourceLabel(route.scaler);
        label += " " + depthLabel(route);
        return label;
    }

    void paintRoutes(juce::Graphics& g, juce::Rectangle<int> area) const
    {
        const auto columns = routeColumnsForWidth(getWidth());
        const auto routeWidth = (area.getWidth() - (columns - 1) * routeGap) / columns;
        const auto visibleCapacity = std::max(1, columns * routeRows);

        if (routeView.activeRoutes.empty())
        {
            auto emptyArea = area.removeFromTop(routeHeight);
            g.setColour(fieldBg);
            g.fillRoundedRectangle(emptyArea.toFloat(), 5.0f);
            g.setColour(strokeSoft);
            g.drawRoundedRectangle(emptyArea.toFloat().reduced(0.5f), 5.0f, 1.0f);
            g.setColour(mutedText);
            g.setFont(uiFont(10.5f, true));
            g.drawText("NO ACTIVE ROUTES", emptyArea.reduced(8, 0), juce::Justification::centredLeft, true);
            return;
        }

        const auto routeCount = static_cast<int>(routeView.activeRoutes.size());
        const auto paintCount = std::min(routeCount, visibleCapacity);
        const auto needsOverflowPill = routeCount > visibleCapacity;
        const auto routePills = needsOverflowPill ? std::max(0, paintCount - 1) : paintCount;

        for (int index = 0; index < routePills; ++index)
            paintRoutePill(g, area, columns, routeWidth, index, routeLabel(routeView.activeRoutes[static_cast<std::size_t>(index)]));

        if (needsOverflowPill)
            paintRoutePill(g, area, columns, routeWidth, visibleCapacity - 1,
                           "+" + juce::String(routeCount - routePills) + " MORE");
    }

    void paintRoutePill(juce::Graphics& g,
                        juce::Rectangle<int> area,
                        int columns,
                        int routeWidth,
                        int index,
                        const juce::String& label) const
    {
        const auto column = index % columns;
        const auto row = index / columns;
        auto routeArea = juce::Rectangle<int>(
            area.getX() + column * (routeWidth + routeGap),
            area.getY() + row * (routeHeight + routeGap),
            routeWidth,
            routeHeight);

        g.setColour(accent.withAlpha(0.12f));
        g.fillRoundedRectangle(routeArea.toFloat(), 5.0f);
        g.setColour(accent.withAlpha(0.62f));
        g.drawRoundedRectangle(routeArea.toFloat().reduced(0.5f), 5.0f, 1.0f);
        g.setColour(text);
        g.setFont(uiFont(10.5f, true));
        g.drawFittedText(label, routeArea.reduced(8, 0), juce::Justification::centredLeft, 1, 0.64f);
    }

    SynthAudioProcessor& audioProcessor;
    synth::ModulationRouteView routeView;
};

// ============================================================================
// SequencerPanel: dense real controls for arp steps and chord voices.
// ============================================================================
class SynthAudioProcessorEditor::SequencerPanel final : public LayoutSection
{
public:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    explicit SequencerPanel(juce::AudioProcessorValueTreeState& state)
        : valueTreeState(state)
    {
        for (const auto* id : {
                 "arp.enabled", "arp.mode", "arp.rate", "arp.gate",
                 "arp.octaves", "arp.hold", "arp.swing", "arp.step_count"
             })
        {
            if (const auto* spec = synth::findParameterSpec(id))
            {
                auto control = std::make_unique<ParameterControl>(valueTreeState, *spec, "Arp ");
                addAndMakeVisible(*control);
                topControls.push_back(std::move(control));
            }
        }

        for (int step = 1; step <= synth::arpStepCount; ++step)
        {
            const auto index = static_cast<std::size_t>(step - 1);
            const auto prefix = "arp.step." + std::to_string(step) + ".";
            configureToggle(steps[index].enabled, prefix + "enabled", "Step " + juce::String(step) + " on");
            configureSlider(steps[index].pitch, prefix + "pitch_semitones", "Step " + juce::String(step) + " pitch");
            configureSlider(steps[index].velocity, prefix + "velocity", "Step " + juce::String(step) + " velocity");
            configureSlider(steps[index].gate, prefix + "gate", "Step " + juce::String(step) + " gate");
            configureToggle(steps[index].tie, prefix + "tie", "Step " + juce::String(step) + " tie");
        }

        for (int voice = 1; voice <= synth::chordVoiceCount; ++voice)
        {
            const auto index = static_cast<std::size_t>(voice - 1);
            const auto prefix = "chord.voice." + std::to_string(voice) + ".";
            configureToggle(chordVoices[index].enabled, prefix + "enabled", "Chord voice " + juce::String(voice) + " on");
            configureSlider(chordVoices[index].pitch, prefix + "pitch_semitones", "Chord voice " + juce::String(voice) + " pitch");
            configureSlider(chordVoices[index].velocity, prefix + "velocity", "Chord voice " + juce::String(voice) + " velocity");
        }

        // Do not strip the "Chord " prefix here: these share the arp top-control flow, so
        // they must read "Chord Enabled" / "Chord Voice Count" to avoid colliding with the
        // arp "Enabled" toggle that sits a few cells to the left.
        if (const auto* spec = synth::findParameterSpec("chord.enabled"))
            chordTopControls.push_back(makeTopControl(*spec, {}));
        if (const auto* spec = synth::findParameterSpec("chord.voice_count"))
            chordTopControls.push_back(makeTopControl(*spec, {}));
    }

    int preferredHeight(int width) const override
    {
        return computeLayoutHeight(width);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        paintPanelBody(g, bounds);

        auto headerArea = getLocalBounds().removeFromTop(headerHeight);
        paintCaptionBar(g, headerArea);

        paintModuleHeaderTick(g, getLocalBounds().removeFromTop(headerHeight), zoneSource);
        auto titleArea = getLocalBounds().removeFromTop(headerHeight).reduced(16, 0);
        auto badgeArea = titleArea.removeFromRight(54).withSizeKeepingCentre(54, 16);
        titleArea.removeFromRight(8);

        g.setColour(text);
        g.setFont(uiFont(12.0f, true));
        g.drawText("ARP / STEP / CHORD", titleArea, juce::Justification::centredLeft, true);

        g.setColour(live.withAlpha(0.18f));
        g.fillRoundedRectangle(badgeArea.toFloat(), 8.0f);
        g.setColour(live);
        g.setFont(uiFont(10.0f, true));
        g.drawText("LIVE", badgeArea, juce::Justification::centred, false);

        paintGridLabels(g);
    }

    void resized() override
    {
        layoutControls(getWidth());
    }

private:
    struct GridSlider
    {
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct GridToggle
    {
        juce::ToggleButton toggle;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    struct StepControls
    {
        GridToggle enabled;
        GridSlider pitch;
        GridSlider velocity;
        GridSlider gate;
        GridToggle tie;
    };

    struct ChordVoiceControls
    {
        GridToggle enabled;
        GridSlider pitch;
        GridSlider velocity;
    };

    std::unique_ptr<ParameterControl> makeTopControl(const synth::ParameterSpec& spec,
                                                     const juce::String& stripPrefix)
    {
        auto control = std::make_unique<ParameterControl>(valueTreeState, spec, stripPrefix);
        addAndMakeVisible(*control);
        return control;
    }

    void configureToggle(GridToggle& control, const std::string& id, const juce::String& tooltip)
    {
        if (synth::findParameterSpec(id) == nullptr)
            return;

        control.toggle.setClickingTogglesState(true);
        control.toggle.setTooltip(tooltip);
        control.attachment = std::make_unique<ButtonAttachment>(valueTreeState, id, control.toggle);
        addAndMakeVisible(control.toggle);
    }

    void configureSlider(GridSlider& control, const std::string& id, const juce::String& tooltip)
    {
        if (const auto* found = synth::findParameterSpec(id))
        {
            const auto spec = *found;
            control.slider.setSliderStyle(juce::Slider::LinearBar);
            control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
            control.slider.setColour(juce::Slider::backgroundColourId, lcdBgEdge);
            control.slider.setColour(juce::Slider::trackColourId, lcdText.withAlpha(0.62f));
            control.slider.setColour(juce::Slider::textBoxTextColourId, lcdText);
            control.slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            control.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            control.slider.setTooltip(tooltip);
            control.slider.setNumDecimalPlacesToDisplay(0);
            tagSliderReadout(control.slider, id, juce::String(spec.name));
            control.attachment = std::make_unique<SliderAttachment>(valueTreeState, id, control.slider);
            control.slider.textFromValueFunction = [spec](double value) { return formatGridValue(spec, value); };
            control.slider.valueFromTextFunction = [spec](const juce::String& textValue) {
                return parseValueText(spec, textValue);
            };
            control.slider.updateText();
            addAndMakeVisible(control.slider);
        }
    }

    int computeTopControlsLayout(juce::Rectangle<int> bounds, bool apply)
    {
        const auto columns = std::max(1, (bounds.getWidth() + controlGap) / (topControlWidth + controlGap));
        const auto columnWidth = static_cast<float>(bounds.getWidth() - (columns - 1) * controlGap)
                                 / static_cast<float>(columns);

        int column = 0;
        int row = 0;
        const auto placeControl = [&](ParameterControl& control) {
            const auto span = std::min(control.isWide() ? 2 : 1, columns);
            if (column + span > columns)
            {
                column = 0;
                ++row;
            }

            if (apply)
            {
                const auto x = bounds.getX() + static_cast<int>(std::round(static_cast<float>(column)
                                                                           * (columnWidth + static_cast<float>(controlGap))));
                const auto width = static_cast<int>(std::round(static_cast<float>(span) * columnWidth
                                                              + static_cast<float>((span - 1) * controlGap)));
                const auto y = bounds.getY() + row * (topControlHeight + rowGap);
                control.setBounds(x, y, width, topControlHeight);
            }

            column += span;
        };

        for (const auto& control : topControls)
            placeControl(*control);
        for (const auto& control : chordTopControls)
            placeControl(*control);

        return (row + 1) * topControlHeight + row * rowGap;
    }

    void layoutStepGrid(juce::Rectangle<int> bounds)
    {
        const auto stepWidth = std::max(28, (bounds.getWidth() - rowLabelWidth - (synth::arpStepCount - 1) * cellGap)
                                            / synth::arpStepCount);
        const auto gridWidth = rowLabelWidth + synth::arpStepCount * stepWidth + (synth::arpStepCount - 1) * cellGap;
        const auto xStart = bounds.getX() + std::max(0, (bounds.getWidth() - gridWidth) / 2);
        auto y = bounds.getY();

        for (int step = 0; step < synth::arpStepCount; ++step)
        {
            const auto x = xStart + rowLabelWidth + step * (stepWidth + cellGap);
            const auto index = static_cast<std::size_t>(step);
            steps[index].enabled.toggle.setBounds(x, y + headerRowHeight, stepWidth, toggleRowHeight);
            steps[index].pitch.slider.setBounds(x, y + headerRowHeight + toggleRowHeight + rowGap,
                                                stepWidth, sliderRowHeight);
            steps[index].velocity.slider.setBounds(x, y + headerRowHeight + toggleRowHeight + sliderRowHeight + 2 * rowGap,
                                                   stepWidth, sliderRowHeight);
            steps[index].gate.slider.setBounds(x, y + headerRowHeight + toggleRowHeight + 2 * sliderRowHeight + 3 * rowGap,
                                               stepWidth, sliderRowHeight);
            steps[index].tie.toggle.setBounds(x, y + headerRowHeight + toggleRowHeight + 3 * sliderRowHeight + 4 * rowGap,
                                              stepWidth, toggleRowHeight);
        }
    }

    void layoutChordGrid(juce::Rectangle<int> bounds)
    {
        const auto voiceWidth = std::max(48, (bounds.getWidth() - rowLabelWidth - (synth::chordVoiceCount - 1) * cellGap)
                                             / synth::chordVoiceCount);
        const auto gridWidth = rowLabelWidth + synth::chordVoiceCount * voiceWidth + (synth::chordVoiceCount - 1) * cellGap;
        const auto xStart = bounds.getX() + std::max(0, (bounds.getWidth() - gridWidth) / 2);
        auto y = bounds.getY();

        for (int voice = 0; voice < synth::chordVoiceCount; ++voice)
        {
            const auto x = xStart + rowLabelWidth + voice * (voiceWidth + cellGap);
            const auto index = static_cast<std::size_t>(voice);
            chordVoices[index].enabled.toggle.setBounds(x, y + headerRowHeight, voiceWidth, toggleRowHeight);
            chordVoices[index].pitch.slider.setBounds(x, y + headerRowHeight + toggleRowHeight + rowGap,
                                                      voiceWidth, sliderRowHeight);
            chordVoices[index].velocity.slider.setBounds(x, y + headerRowHeight + toggleRowHeight + sliderRowHeight + 2 * rowGap,
                                                         voiceWidth, sliderRowHeight);
        }
    }

    int computeLayoutHeight(int width) const
    {
        if (width <= 0)
            return headerHeight + padTop + topControlHeight + sectionGap + stepGridHeight + sectionGap
                   + chordGridHeight + padBottom;

        auto bounds = juce::Rectangle<int>(0, 0, width, 10000).reduced(padX, 0);
        auto y = headerHeight + padTop;
        y += computeTopControlsHeight(bounds.withY(y).withHeight(topControlHeight * 2)) + sectionGap;
        y += stepGridHeight + sectionGap;
        y += chordGridHeight + padBottom;
        return y;
    }

    void layoutControls(int width)
    {
        if (width <= 0)
            return;

        auto bounds = juce::Rectangle<int>(0, 0, width, 10000).reduced(padX, 0);
        auto y = headerHeight + padTop;
        y += computeTopControlsLayout(bounds.withY(y).withHeight(topControlHeight * 2), true) + sectionGap;
        layoutStepGrid(bounds.withY(y).withHeight(stepGridHeight));
        y += stepGridHeight + sectionGap;
        layoutChordGrid(bounds.withY(y).withHeight(chordGridHeight));
    }

    void paintGridLabels(juce::Graphics& g) const
    {
        auto bounds = getLocalBounds().reduced(padX, 0);
        auto y = headerHeight + padTop;
        y += computeTopControlsHeight(bounds.withY(y).withHeight(topControlHeight * 2)) + sectionGap;
        paintStepLabels(g, bounds.withY(y).withHeight(stepGridHeight));
        y += stepGridHeight + sectionGap;
        paintChordLabels(g, bounds.withY(y).withHeight(chordGridHeight));
    }

    int computeTopControlsHeight(juce::Rectangle<int> bounds) const
    {
        const auto columns = std::max(1, (bounds.getWidth() + controlGap) / (topControlWidth + controlGap));
        int column = 0;
        int row = 0;
        auto countControl = [&column, &row, columns](const ParameterControl& control) {
            const auto span = std::min(control.isWide() ? 2 : 1, columns);
            if (column + span > columns)
            {
                column = 0;
                ++row;
            }
            column += span;
        };

        for (const auto& control : topControls)
            countControl(*control);
        for (const auto& control : chordTopControls)
            countControl(*control);

        return (row + 1) * topControlHeight + row * rowGap;
    }

    void paintStepLabels(juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        paintGridFrame(g, bounds);

        const auto stepWidth = std::max(28, (bounds.getWidth() - rowLabelWidth - (synth::arpStepCount - 1) * cellGap)
                                            / synth::arpStepCount);
        const auto gridWidth = rowLabelWidth + synth::arpStepCount * stepWidth + (synth::arpStepCount - 1) * cellGap;
        const auto xStart = bounds.getX() + std::max(0, (bounds.getWidth() - gridWidth) / 2);
        auto y = bounds.getY();

        g.setColour(lcdText);
        g.setFont(uiFont(10.5f, true));
        g.drawText("STEP", xStart, y, rowLabelWidth, headerRowHeight, juce::Justification::centredLeft, false);
        for (int step = 0; step < synth::arpStepCount; ++step)
        {
            const auto x = xStart + rowLabelWidth + step * (stepWidth + cellGap);
            g.drawText(juce::String(step + 1), x, y, stepWidth, headerRowHeight, juce::Justification::centred, false);
        }

        y += headerRowHeight;
        paintRowLabel(g, xStart, y, "On");
        y += toggleRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Pitch");
        y += sliderRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Vel");
        y += sliderRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Gate");
        y += sliderRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Tie");
    }

    void paintChordLabels(juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        paintGridFrame(g, bounds);

        const auto voiceWidth = std::max(48, (bounds.getWidth() - rowLabelWidth - (synth::chordVoiceCount - 1) * cellGap)
                                             / synth::chordVoiceCount);
        const auto gridWidth = rowLabelWidth + synth::chordVoiceCount * voiceWidth + (synth::chordVoiceCount - 1) * cellGap;
        const auto xStart = bounds.getX() + std::max(0, (bounds.getWidth() - gridWidth) / 2);
        auto y = bounds.getY();

        g.setColour(lcdText);
        g.setFont(uiFont(10.5f, true));
        g.drawText("CHORD", xStart, y, rowLabelWidth, headerRowHeight, juce::Justification::centredLeft, false);
        for (int voice = 0; voice < synth::chordVoiceCount; ++voice)
        {
            const auto x = xStart + rowLabelWidth + voice * (voiceWidth + cellGap);
            g.drawText(juce::String(voice + 1), x, y, voiceWidth, headerRowHeight, juce::Justification::centred, false);
        }

        y += headerRowHeight;
        paintRowLabel(g, xStart, y, "On");
        y += toggleRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Pitch");
        y += sliderRowHeight + rowGap;
        paintRowLabel(g, xStart, y, "Vel");
    }

    void paintRowLabel(juce::Graphics& g, int x, int y, const juce::String& label) const
    {
        g.setColour(lcdDim);
        g.setFont(uiFont(10.0f, true));
        g.drawText(label, x, y, rowLabelWidth - 6, sliderRowHeight, juce::Justification::centredLeft, false);
    }

    void paintGridFrame(juce::Graphics& g, juce::Rectangle<int> bounds) const
    {
        // Blue LCD screen, like the Sylenth arp/step display: recessed glossy glass.
        const auto frame = bounds.toFloat().reduced(0.5f);
        g.setGradientFill(juce::ColourGradient(lcdBg.brighter(0.10f), 0.0f, frame.getY(),
                                               lcdBgEdge, 0.0f, frame.getBottom(), false));
        g.fillRoundedRectangle(frame, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.fillRoundedRectangle(frame.withHeight(frame.getHeight() * 0.4f).reduced(2.0f, 1.5f), 3.0f);
        g.setColour(lcdStroke);
        g.drawRoundedRectangle(frame, 4.0f, 1.0f);
    }

    juce::AudioProcessorValueTreeState& valueTreeState;
    std::vector<std::unique_ptr<ParameterControl>> topControls;
    std::vector<std::unique_ptr<ParameterControl>> chordTopControls;
    std::array<StepControls, synth::arpStepCount> steps;
    std::array<ChordVoiceControls, synth::chordVoiceCount> chordVoices;

    static constexpr int headerHeight = 26;
    static constexpr int padX = 12;
    static constexpr int padTop = 6;
    static constexpr int padBottom = 10;
    static constexpr int controlGap = 8;
    static constexpr int cellGap = 4;
    static constexpr int rowGap = 4;
    static constexpr int sectionGap = 12;
    static constexpr int topControlWidth = 66;
    static constexpr int topControlHeight = 58;
    static constexpr int rowLabelWidth = 42;
    static constexpr int headerRowHeight = 18;
    static constexpr int toggleRowHeight = 26;
    static constexpr int sliderRowHeight = 40;
    static constexpr int stepGridHeight = headerRowHeight + 2 * toggleRowHeight + 3 * sliderRowHeight + 4 * rowGap;
    static constexpr int chordGridHeight = headerRowHeight + toggleRowHeight + 2 * sliderRowHeight + 2 * rowGap;
};

// ============================================================================
// Local helpers shared by the editor implementation.
// ============================================================================
namespace
{
juce::String fileSafeName(juce::String name)
{
    name = name.trim();
    if (name.isEmpty())
        name = "User Preset";

    std::string safe;
    auto pendingDash = false;
    for (const auto character : name.toStdString())
    {
        const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9'))
        {
            if (pendingDash && !safe.empty())
                safe.push_back('-');
            safe.push_back(lower);
            pendingDash = false;
        }
        else
        {
            pendingDash = true;
        }
    }

    return safe.empty() ? "user-preset" : juce::String(safe);
}

juce::File presetFileWithExtension(juce::File file)
{
    if (file == juce::File()
        || file.getFileExtension().equalsIgnoreCase(".SynthiaPreset"))
        return file;

    return file.withFileExtension(".SynthiaPreset");
}

juce::String nonEmptyPresetField(const juce::String& value, const juce::String& fallback)
{
    const auto trimmed = value.trim();
    return trimmed.isNotEmpty() ? trimmed : fallback;
}

juce::String presetMenuSection(const SynthAudioProcessor::PresetListItem& item)
{
    if (!item.valid)
        return nonEmptyPresetField(item.sourceLabel, "Preset") + " / Invalid";

    return nonEmptyPresetField(item.sourceLabel, "Preset")
           + " / " + nonEmptyPresetField(item.bank, "Unbanked");
}

juce::String presetMenuLabel(const SynthAudioProcessor::PresetListItem& item)
{
    auto name = nonEmptyPresetField(item.displayName, item.file.getFileNameWithoutExtension());
    if (!item.valid)
        return "INVALID / " + name + " - " + nonEmptyPresetField(item.validationMessage, "Preset validation failed");

    auto label = nonEmptyPresetField(item.sourceLabel, "Preset")
                 + " / " + nonEmptyPresetField(item.category, "Uncategorized")
                 + " - " + name;

    if (item.favorite)
        label = "FAV  " + label;

    if (!item.tags.isEmpty())
    {
        juce::StringArray visibleTags;
        for (int i = 0; i < juce::jmin(2, item.tags.size()); ++i)
            visibleTags.add(item.tags[i]);

        label << "  [" << visibleTags.joinIntoString(", ") << "]";
    }

    return label;
}
} // namespace

// ============================================================================
// Editor
// ============================================================================
SynthAudioProcessorEditor::SynthAudioProcessorEditor(SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    lookAndFeel = std::make_unique<SynthLookAndFeel>();
    setLookAndFeel(lookAndFeel.get());

    meter = std::make_unique<Meter>();

    buildHeader();
    buildLayerBar();
    buildPages();
    wireModulationTargets();

    refreshPresetMenu();
    setSelectedLayer(0);
    setPage(Page::Sound);
    updateDiagnostics();
    refreshPresetWorkflow();
    syncPresetMetadataPanel();
    refreshModulationVisuals();
    startTimerHz(15);

    // Listen to every nested control so touching a knob can echo "name = value" in the LCD.
    addMouseListener(this, true);

    setResizable(true, true);
    setResizeLimits(1080, 760, 1800, 1320);
    setSize(1320, 940);

    // Headless UI proof: SYNTHIA_UI_SNAPSHOT=<path.png> renders the editor
    // offscreen after it settles (no screen-recording permission needed) so
    // UI states can be captured for QA/docs from scripts. No-op otherwise.
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("SYNTHIA_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty())
    {
        // Optional preset to capture (e.g. step-LFO or modulation states).
        const auto snapshotPreset =
            juce::SystemStats::getEnvironmentVariable("SYNTHIA_UI_SNAPSHOT_PRESET", {});
        if (snapshotPreset.isNotEmpty())
        {
            juce::String presetMessage;
            audioProcessor.loadPresetFile(juce::File(snapshotPreset), presetMessage);
        }

        juce::Timer::callAfterDelay(900, [safeThis = juce::Component::SafePointer<SynthAudioProcessorEditor>(this),
                                          snapshotPath] {
            if (safeThis == nullptr)
                return;
            juce::Image image(juce::Image::ARGB, safeThis->getWidth(), safeThis->getHeight(), true);
            juce::Graphics graphics(image);
            safeThis->paintEntireComponent(graphics, true);
            juce::File file(snapshotPath);
            file.deleteFile();
            juce::FileOutputStream stream(file);
            if (stream.openedOk())
                juce::PNGImageFormat().writeImageToStream(image, stream);
            if (juce::SystemStats::getEnvironmentVariable("SYNTHIA_UI_SNAPSHOT_QUIT", {}) == "1")
                juce::JUCEApplicationBase::quit();
        });
    }
}

SynthAudioProcessorEditor::~SynthAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SynthAudioProcessorEditor::buildHeader()
{
    titleLabel.setText("SYNTHIA", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, text);
    titleLabel.setFont(uiFont(18.0f, true));
    addAndMakeVisible(titleLabel);

    engineTag.setText("CORE OSC ENGINE", juce::dontSendNotification);
    engineTag.setColour(juce::Label::textColourId, live);
    engineTag.setFont(uiFont(10.0f, true));
    addAndMakeVisible(engineTag);

    styleFlatButton(prevPresetButton, buttonBg);
    styleFlatButton(nextPresetButton, buttonBg);
    styleFlatButton(loadButton, accent.darker(0.32f));
    styleFlatButton(saveButton, buttonRaised);
    styleFlatButton(duplicateButton, buttonRaised);
    styleFlatButton(panicButton, warn.darker(0.1f));
    dirtyStatePill.setJustificationType(juce::Justification::centred);
    dirtyStatePill.setFont(uiFont(10.5f, true));
    addAndMakeVisible(prevPresetButton);
    addAndMakeVisible(nextPresetButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(duplicateButton);
    addAndMakeVisible(panicButton);
    addAndMakeVisible(dirtyStatePill);

    addAndMakeVisible(presetCombo);
    presetCombo.setTextWhenNothingSelected("Select preset");

    nameEditor.setText(audioProcessor.getCurrentPresetName());
    nameEditor.setFont(uiFont(13.0f));
    addAndMakeVisible(nameEditor);

    addAndMakeVisible(*meter);

    voicesCaption.setText("VOICES", juce::dontSendNotification);
    styleChipCaption(voicesCaption);
    styleChipValue(voicesValue);
    addAndMakeVisible(voicesCaption);
    addAndMakeVisible(voicesValue);

    costCaption.setText("LOAD EST.", juce::dontSendNotification);
    styleChipCaption(costCaption);
    styleChipValue(costValue);
    addAndMakeVisible(costCaption);
    addAndMakeVisible(costValue);

    prevPresetButton.onClick = [this] { stepPreset(-1); };
    nextPresetButton.onClick = [this] { stepPreset(1); };
    loadButton.onClick = [this] { loadSelectedPreset(); };
    saveButton.onClick = [this] { savePresetAs(); };
    duplicateButton.onClick = [this] { duplicatePreset(); };
    panicButton.onClick = [this] {
        audioProcessor.requestPanic();
        updateStatus("Panic sent: voices and FX cleared");
    };

    statusLabel.setColour(juce::Label::textColourId, mutedText);
    statusLabel.setFont(uiFont(11.5f));
    statusLabel.setText("Ready", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);

    diagnosticsLabel.setJustificationType(juce::Justification::centredRight);
    diagnosticsLabel.setColour(juce::Label::textColourId, mutedText);
    diagnosticsLabel.setFont(uiFont(11.5f));
    addAndMakeVisible(diagnosticsLabel);
}

void SynthAudioProcessorEditor::buildLayerBar()
{
    layerCaption.setText("PART", juce::dontSendNotification);
    layerCaption.setColour(juce::Label::textColourId, mutedText);
    layerCaption.setFont(uiFont(11.0f, true));
    layerCaption.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(layerCaption);

    auto styleLayerButton = [this](juce::TextButton& button) {
        styleFlatButton(button, buttonBg);
        button.setClickingTogglesState(false);
        addAndMakeVisible(button);
    };
    styleLayerButton(layerAButton);
    styleLayerButton(layerBButton);
    layerAButton.onClick = [this] { setSelectedLayer(0); };
    layerBButton.onClick = [this] { setSelectedLayer(1); };

    layerStatusPill.setJustificationType(juce::Justification::centred);
    layerStatusPill.setFont(uiFont(11.0f, true));
    addAndMakeVisible(layerStatusPill);

    addAndMakeVisible(layerControlsHost);

    soundTab.setClickingTogglesState(false);
    modTab.setClickingTogglesState(false);
    fxTab.setClickingTogglesState(false);
    browserTab.setClickingTogglesState(false);
    soundTab.onClick = [this] { setPage(Page::Sound); };
    modTab.onClick = [this] { setPage(Page::Mod); };
    fxTab.onClick = [this] { setPage(Page::Fx); };
    browserTab.onClick = [this] { setPage(Page::Browser); };
    addAndMakeVisible(soundTab);
    addAndMakeVisible(modTab);
    addAndMakeVisible(fxTab);
    addAndMakeVisible(browserTab);

    pageViewport.setScrollBarsShown(true, false);
    pageViewport.setViewedComponent(&soundPage, false);
    addAndMakeVisible(pageViewport);
}

SynthAudioProcessorEditor::Panel* SynthAudioProcessorEditor::addPanel(
    juce::Component& page,
    std::vector<std::unique_ptr<Panel>>& store,
    juce::String title,
    const std::vector<std::string>& ids,
    juce::String badge,
    juce::Colour badgeColour,
    const juce::String& stripPrefix,
    const juce::String& enabledParamId)
{
    auto panel = std::make_unique<Panel>(audioProcessor.getValueTreeState(), std::move(title),
                                         ids, std::move(badge), badgeColour, stripPrefix,
                                         enabledParamId);
    auto* raw = panel.get();
    page.addAndMakeVisible(*raw);
    store.push_back(std::move(panel));
    return raw;
}

void SynthAudioProcessorEditor::buildPages()
{
    // ---- SOUND: the live core sound-design surface ------------------------
    // Blue LCD preset screen + MIXER flank the filter, like the Sylenth centre row.
    lcdDisplay = std::make_unique<LcdDisplay>(audioProcessor);
    soundPage.addAndMakeVisible(*lcdDisplay);
    mixerPanel = std::make_unique<MixerPanel>(audioProcessor.getValueTreeState(), *meter);
    soundPage.addAndMakeVisible(*mixerPanel);

    // Preset/program workflow lives on its own Browser page so the Sound page can hold the
    // synthesis engine at default size with minimal scrolling, and the preset surface reads
    // as one integrated program workflow rather than form panels stacked under the synth.
    presetWorkflowPanel = std::make_unique<PresetWorkflowPanel>(*this);
    browserPage.addAndMakeVisible(*presetWorkflowPanel);
    presetMetadataPanel = std::make_unique<PresetMetadataPanel>(*this);
    browserPage.addAndMakeVisible(*presetMetadataPanel);
    presetBrowserPanel = std::make_unique<PresetBrowserPanel>(*this);
    browserPage.addAndMakeVisible(*presetBrowserPanel);
    midiControllerPanel = std::make_unique<MidiControllerPanel>(*this);
    browserPage.addAndMakeVisible(*midiControllerPanel);

    // Legacy flat osc.* is the audible tone source that Osc A1's slot gates and mixes.
    // The honest title makes that A1 relationship clear instead of a second "Oscillator".
    coreOscPanel = addPanel(soundPage, soundPanels, "Osc A1 Tone", {
        "osc.pitch_semitones", "osc.fine_cents", "osc.stack_count", "osc.stack_detune",
        "osc.saw_level", "osc.pulse_level", "osc.pulse_width", "osc.noise_level",
        "osc.sub_wave", "osc.sub_octave", "osc.sub_level", "osc.sub_pulse_width",
        "osc.sync_amount", "osc.phase_reset"
    }, "LIVE", zoneSource);

    // Bespoke Sylenth FILTER module (TYPE selector + cutoff/reso/drive/keytrack knobs +
    // quality), binding the same real filter.* parameters as before.
    filterPanel = std::make_unique<FilterPanel>(audioProcessor.getValueTreeState());
    soundPage.addAndMakeVisible(*filterPanel);

    // Amp/Mod envelopes render as Sylenth-style vertical ADSR faders with a derived
    // contour. Same APVTS parameters as before, presented as a hardware envelope module.
    ampEnvPanel = std::make_unique<EnvelopePanel>(audioProcessor.getValueTreeState(),
                                                  "Amp Env", zoneShape, "amp_env.");
    soundPage.addAndMakeVisible(*ampEnvPanel);

    modEnvPanel = std::make_unique<EnvelopePanel>(audioProcessor.getValueTreeState(),
                                                  "Mod Env", zoneShape, "mod_env.");
    soundPage.addAndMakeVisible(*modEnvPanel);

    // LFO as a visual module: bound controls plus a live display whose Step
    // shape is a drawable grid over the real lfo.step.* parameters.
    lfoModulePanel = std::make_unique<LfoModulePanel>(audioProcessor);
    soundPage.addAndMakeVisible(*lfoModulePanel);

    // Draggable modulation source chips; drops land on halo-capable knobs.
    modSourcesPanel = std::make_unique<ModSourcesPanel>();
    soundPage.addAndMakeVisible(*modSourcesPanel);

    voicePanel = addPanel(soundPage, soundPanels, "Voice", {
        "voice.mode", "voice.polyphony", "voice.unison_count", "voice.retrigger",
        "voice.glide_ms", "voice.velocity_glide_ms"
    }, {}, zonePerform);

    ampPanel = addPanel(soundPage, soundPanels, "Amp / Stereo", {
        "amp.drive", "amp.level_db", "amp.pan", "amp.pan_spread",
        "amp.unison_spread", "amp.analog"
    }, {}, zonePerform);

    rampPanel = addPanel(soundPage, soundPanels, "Ramp", {
        "ramp.enabled", "ramp.mode", "ramp.delay_ms", "ramp.rise_ms", "ramp.curve"
    }, {}, zonePerform);

    sequencerPanel = std::make_unique<SequencerPanel>(audioProcessor.getValueTreeState());
    soundPage.addAndMakeVisible(*sequencerPanel);

    macroPanel = addPanel(soundPage, soundPanels, "Macros", {
        "macro.motion", "macro.width", "macro.drive", "macro.space"
    }, {}, zonePerform);

    // ---- MOD: read-only route overview, direct routes, and eight TransMod slots
    modulationOverviewPanel = std::make_unique<ModulationOverviewPanel>(audioProcessor);
    modPage.addAndMakeVisible(*modulationOverviewPanel);

    oscPitchModPanel = addPanel(modPage, modPanels, "Osc Pitch Mod", {
        "direct.osc_keytrack_semitones", "direct.osc_lfo_semitones", "direct.osc_mod_env_semitones"
    }, {}, zoneMod, "Osc ");
    pulseWidthModPanel = addPanel(modPage, modPanels, "Pulse Width Mod", {
        "direct.pulse_keytrack", "direct.pulse_lfo", "direct.pulse_mod_env"
    }, {}, zoneMod, "Pulse ");
    filterCutoffModPanel = addPanel(modPage, modPanels, "Filter Cutoff Mod", {
        "direct.filter_keytrack", "direct.filter_lfo_semitones", "direct.filter_mod_env_semitones"
    }, {}, zoneMod, "Filter ");

    for (int slot = 1; slot <= synth::transModSlotCount; ++slot)
    {
        const auto prefix = "transmod." + std::to_string(slot) + ".";
        const auto title = "TransMod " + juce::String(slot);
        transModPanels[static_cast<std::size_t>(slot - 1)] = addPanel(
            modPage, modPanels, title, {
                prefix + "enabled", prefix + "source", prefix + "scaler",
                prefix + "osc_pitch_semitones", prefix + "pulse_width",
                prefix + "filter_cutoff_semitones", prefix + "amp_level_db", prefix + "pan"
            }, {}, zoneMod, title + " ", prefix + "enabled");
    }

    // ---- FX: a post-voice rack grouped by module, plus master/quality -----
    // Each module passes its enable param so the header power dot/dim reflects bypass.
    saturationPanel = addPanel(fxPage, fxPanels, "01 Saturation", {
        "fx.saturation_enabled", "fx.distortion_mode", "fx.saturation_mix", "fx.saturation_drive"
    }, "DRIVE", staged, "Saturation ", "fx.saturation_enabled");
    phaserPanel = addPanel(fxPage, fxPanels, "02 Phaser", {
        "fx.phaser_enabled", "fx.phaser_mix", "fx.phaser_rate_hz",
        "fx.phaser_depth", "fx.phaser_feedback"
    }, "PHASE", accent, "Phaser ", "fx.phaser_enabled");
    chorusPanel = addPanel(fxPage, fxPanels, "03 Chorus", {
        "fx.chorus_enabled", "fx.chorus_mix", "fx.chorus_rate_hz", "fx.chorus_depth_ms"
    }, "MOD", accent, "Chorus ", "fx.chorus_enabled");
    eqPanel = addPanel(fxPage, fxPanels, "04 EQ", {
        "fx.eq_enabled", "fx.eq_low_gain_db", "fx.eq_high_gain_db"
    }, "TONE", live, "EQ ", "fx.eq_enabled");
    delayPanel = addPanel(fxPage, fxPanels, "05 Delay", {
        "fx.delay_enabled", "fx.delay_mix", "fx.delay_sync_division", "fx.delay_feedback"
    }, "ECHO", accent, "Delay ", "fx.delay_enabled");
    reverbPanel = addPanel(fxPage, fxPanels, "06 Reverb", {
        "fx.reverb_enabled", "fx.reverb_mix", "fx.reverb_decay"
    }, "SPACE", accent, "Reverb ", "fx.reverb_enabled");
    compressorPanel = addPanel(fxPage, fxPanels, "07 Compressor", {
        "fx.compressor_enabled", "fx.compressor_threshold_db", "fx.compressor_ratio",
        "fx.compressor_makeup_db", "fx.compressor_mix"
    }, "DYN", staged, "Compressor ", "fx.compressor_enabled");
    masterFxPanel = addPanel(fxPage, fxPanels, "08 Master / Quality", {
        "fx.enabled", "quality.realtime_mode", "quality.offline_mode"
    }, "HOST", live, {}, "fx.enabled");
}

void SynthAudioProcessorEditor::setSelectedLayer(int layerIndex)
{
    selectedLayer = juce::jlimit(0, synth::layerCount - 1, layerIndex);
    const auto layerNumber = selectedLayer + 1;
    const auto layerLetter = juce::String::charToString(static_cast<juce::juce_wchar>('A' + selectedLayer));
    const auto layerPrefix = "layer." + juce::String(layerNumber) + ".";
    const auto stripPrefix = "Layer " + layerLetter + " ";

    // Lit active part capsule; dark text for contrast on the accent fill.
    const auto activeColour = accent.darker(0.08f);
    const auto activeText = onAccentText;
    const auto inactiveColour = buttonBg;
    styleFlatButton(layerAButton, selectedLayer == 0 ? activeColour : inactiveColour,
                    selectedLayer == 0 ? activeText : text);
    styleFlatButton(layerBButton, selectedLayer == 1 ? activeColour : inactiveColour,
                    selectedLayer == 1 ? activeText : text);

    // Render-boundary truth: layer mix and four slot outputs are audible, while A1
    // still uses the legacy flat osc.* controls as its compatibility source.
    layerStatusPill.setText(selectedLayer == 0 ? "LIVE - compat path" : "LIVE - layer mix",
                            juce::dontSendNotification);
    layerStatusPill.setColour(juce::Label::textColourId, live);

    // Rebuild per-layer mix controls for the selected layer.
    layerControls.clear();
    auto& state = audioProcessor.getValueTreeState();
    for (const auto* suffix : { "enabled", "level_db", "pan", "solo", "mute" })
    {
        const auto id = (layerPrefix + suffix).toStdString();
        if (const auto* spec = synth::findParameterSpec(id))
        {
            auto control = std::make_unique<ParameterControl>(state, *spec, stripPrefix);
            layerControlsHost.addAndMakeVisible(*control);
            layerControls.push_back(std::move(control));
        }
    }

    // Rebuild the two oscillator-slot panels for the selected layer. These bind to real
    // layer.N.osc.M.* state. A1 gates and mixes the legacy osc.* compatibility source;
    // the other slots render through the current oscillator stack foundation.
    for (int slot = 0; slot < synth::oscillatorSlotsPerLayer; ++slot)
    {
        if (slotPanels[static_cast<std::size_t>(slot)] != nullptr)
            soundPage.removeChildComponent(slotPanels[static_cast<std::size_t>(slot)].get());

        const auto slotNumber = slot + 1;
        const auto oscPrefix = layerPrefix + "osc." + juce::String(slotNumber) + ".";
        const auto slotTitle = "Oscillator " + layerLetter + juce::String(slotNumber);

        auto panel = std::make_unique<OscillatorPanel>(state, oscPrefix, slotTitle,
                                                       oscPrefix + "enabled");
        soundPage.addAndMakeVisible(*panel);
        slotPanels[static_cast<std::size_t>(slot)] = std::move(panel);
    }

    resized();
}

void SynthAudioProcessorEditor::setPage(Page page)
{
    currentPage = page;

    juce::Component* viewed = &soundPage;
    if (page == Page::Mod)
        viewed = &modPage;
    else if (page == Page::Fx)
        viewed = &fxPage;
    else if (page == Page::Browser)
        viewed = &browserPage;
    pageViewport.setViewedComponent(viewed, false);

    if (page == Page::Mod && modulationOverviewPanel != nullptr)
        modulationOverviewPanel->refresh();

    auto styleTab = [](juce::TextButton& tab, bool active) {
        styleFlatButton(tab, active ? accent.darker(0.18f) : buttonBg,
                        active ? onAccentText : mutedText);
    };
    styleTab(soundTab, page == Page::Sound);
    styleTab(modTab, page == Page::Mod);
    styleTab(fxTab, page == Page::Fx);
    styleTab(browserTab, page == Page::Browser);

    layoutActivePage();
}

void SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Flat midnight body; modules supply their own cards on top.
    g.fillAll(background);

    // Performance strip: the preset header and part/layer row share one darker
    // flush band, separated from the workspace by a single hairline.
    const auto stripBottom = headerHeight + layerBarHeight;
    g.setColour(headerBg);
    g.fillRect(getLocalBounds().removeFromTop(stripBottom));
    g.setColour(strokeSoft);
    g.drawHorizontalLine(headerHeight, 0.0f, static_cast<float>(getWidth()));
    g.setColour(stroke.withAlpha(0.8f));
    g.drawHorizontalLine(stripBottom, 0.0f, static_cast<float>(getWidth()));

    // Selected-part accent: a lit underline on the active Part button.
    const auto& activeLayerButton = selectedLayer == 0 ? layerAButton : layerBButton;
    if (!activeLayerButton.getBounds().isEmpty())
    {
        g.setColour(accent);
        g.fillRect(juce::Rectangle<int>(activeLayerButton.getX(), activeLayerButton.getBottom() + 3,
                                        activeLayerButton.getWidth(), 2));
    }

    auto footer = getLocalBounds().removeFromBottom(footerHeight);
    g.setColour(headerBg);
    g.fillRect(footer);
    g.setColour(strokeSoft);
    g.drawHorizontalLine(getHeight() - footerHeight, 0.0f, static_cast<float>(getWidth()));
}

void SynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    layoutHeader(area.removeFromTop(headerHeight));
    layoutLayerBar(area.removeFromTop(layerBarHeight));

    auto footer = area.removeFromBottom(footerHeight).reduced(16, 0);
    diagnosticsLabel.setBounds(footer.removeFromRight(640));
    statusLabel.setBounds(footer);

    auto tabs = area.removeFromTop(tabBarHeight).reduced(16, 6);
    const auto tabWidth = 112;
    soundTab.setBounds(tabs.removeFromLeft(tabWidth));
    tabs.removeFromLeft(6);
    modTab.setBounds(tabs.removeFromLeft(tabWidth));
    tabs.removeFromLeft(6);
    fxTab.setBounds(tabs.removeFromLeft(tabWidth));
    tabs.removeFromLeft(6);
    browserTab.setBounds(tabs.removeFromLeft(tabWidth));

    pageViewport.setBounds(area.reduced(12, 8));
    layoutActivePage();
}

void SynthAudioProcessorEditor::layoutHeader(juce::Rectangle<int> area)
{
    area = area.reduced(16, 9);

    auto left = area.removeFromLeft(132);
    left = left.withSizeKeepingCentre(left.getWidth(), 40);
    titleLabel.setBounds(left.removeFromTop(24));
    engineTag.setBounds(left.removeFromTop(14));

    auto centreInHeight = [&area](juce::Rectangle<int> slot, int height) {
        return slot.withSizeKeepingCentre(slot.getWidth(), height).translated(0, 0)
            .withY(area.getY() + (area.getHeight() - height) / 2);
    };

    // Right cluster: panic and readout chips (the output meter now lives in the layer bar).
    panicButton.setBounds(centreInHeight(area.removeFromRight(74), 30));
    area.removeFromRight(14);

    auto costChip = area.removeFromRight(60);
    costCaption.setBounds(costChip.removeFromTop(costChip.getHeight() / 2));
    costValue.setBounds(costChip);
    area.removeFromRight(6);
    auto voicesChip = area.removeFromRight(60);
    voicesCaption.setBounds(voicesChip.removeFromTop(voicesChip.getHeight() / 2));
    voicesValue.setBounds(voicesChip);
    area.removeFromRight(14);

    dirtyStatePill.setBounds(centreInHeight(area.removeFromRight(70), 22));
    area.removeFromRight(10);

    // Middle cluster: preset nav with a flexible combo.
    auto nav = area;
    const auto compactPresetNav = nav.getWidth() < 650;
    duplicateButton.setVisible(!compactPresetNav);
    nameEditor.setVisible(!compactPresetNav);

    prevPresetButton.setBounds(centreInHeight(nav.removeFromLeft(30), 30));
    nav.removeFromLeft(4);

    if (!compactPresetNav)
    {
        duplicateButton.setBounds(centreInHeight(nav.removeFromRight(88), 30));
        nav.removeFromRight(6);
    }
    else
    {
        duplicateButton.setBounds({});
    }

    saveButton.setBounds(centreInHeight(nav.removeFromRight(74), 30));
    nav.removeFromRight(6);
    loadButton.setBounds(centreInHeight(nav.removeFromRight(58), 30));
    nav.removeFromRight(compactPresetNav ? 8 : 10);

    if (!compactPresetNav)
    {
        nameEditor.setBounds(centreInHeight(nav.removeFromRight(150), 28));
        nav.removeFromRight(8);
    }
    else
    {
        nameEditor.setBounds({});
    }

    nextPresetButton.setBounds(centreInHeight(nav.removeFromRight(30), 30));
    nav.removeFromRight(6);
    presetCombo.setBounds(centreInHeight(nav, 30));
}

void SynthAudioProcessorEditor::layoutLayerBar(juce::Rectangle<int> area)
{
    area = area.reduced(16, 8);

    auto verticalCentre = [&area](juce::Rectangle<int> slot, int height) {
        return slot.withY(area.getY() + (area.getHeight() - height) / 2).withHeight(height);
    };

    layerCaption.setBounds(verticalCentre(area.removeFromLeft(48), 20));
    layerAButton.setBounds(verticalCentre(area.removeFromLeft(40), 30));
    area.removeFromLeft(6);
    layerBButton.setBounds(verticalCentre(area.removeFromLeft(40), 30));
    area.removeFromLeft(14);
    layerStatusPill.setBounds(verticalCentre(area.removeFromLeft(184), 22));
    area.removeFromLeft(8);

    // The output LED meter now lives in the Sound-page MIXER (Sylenth mixer side).
    layerControlsHost.setBounds(area);
    for (int i = 0; i < static_cast<int>(layerControls.size()); ++i)
        layerControls[static_cast<std::size_t>(i)]->setBounds(i * (72 + 6), 0, 72, layerControlsHost.getHeight());
}

int SynthAudioProcessorEditor::layoutRows(juce::Component& page,
                                          const std::vector<std::vector<RowItem>>& rows,
                                          int width)
{
    const auto outerPad = 4;
    const auto rowGap = 5;
    const auto columnGap = 5;
    const auto usableWidth = width - 2 * outerPad;

    int y = outerPad;
    for (const auto& row : rows)
    {
        const auto totalGap = columnGap * std::max(0, static_cast<int>(row.size()) - 1);
        const auto contentWidth = usableWidth - totalGap;

        int rowHeight = 0;
        std::vector<int> widths;
        widths.reserve(row.size());
        for (const auto& item : row)
        {
            const auto itemWidth = static_cast<int>(std::round(item.widthFraction * static_cast<float>(contentWidth)));
            widths.push_back(itemWidth);
            rowHeight = std::max(rowHeight, item.section->preferredHeight(itemWidth));
        }

        int x = outerPad;
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            row[i].section->setBounds(x, y, widths[i], rowHeight);
            x += widths[i] + columnGap;
        }

        y += rowHeight + rowGap;
    }

    page.setSize(width, y - rowGap + outerPad);
    return page.getHeight();
}

void SynthAudioProcessorEditor::layoutActivePage()
{
    const auto viewWidth = std::max(900, pageViewport.getMaximumVisibleWidth());

    if (currentPage == Page::Sound)
    {
        if (slotPanels[0] == nullptr || slotPanels[1] == nullptr || coreOscPanel == nullptr
            || ampEnvPanel == nullptr || modEnvPanel == nullptr || lcdDisplay == nullptr
            || mixerPanel == nullptr || sequencerPanel == nullptr || filterPanel == nullptr
            || lfoModulePanel == nullptr || modSourcesPanel == nullptr)
            return;
        // The Sound page is the synthesis engine: oscillators around the amp envelope
        // on top, the Filter | centre display | Mixer row, then the mod-env/LFO
        // shaping row with the LFO display as the hero, the legacy A1 tone source,
        // performance modules, and the arp/step/chord grid. Preset/MIDI workflow
        // lives on the Browser page, so this page stays close to one screen.
        std::vector<std::vector<RowItem>> rows = {
            { { slotPanels[0].get(), 0.40f }, { ampEnvPanel.get(), 0.20f }, { slotPanels[1].get(), 0.40f } },
            { { filterPanel.get(), 0.30f }, { lcdDisplay.get(), 0.40f }, { mixerPanel.get(), 0.30f } },
            { { modEnvPanel.get(), 0.22f }, { lfoModulePanel.get(), 0.50f }, { voicePanel, 0.28f } },
            { { modSourcesPanel.get(), 1.0f } },
            { { coreOscPanel, 1.0f } },
            { { rampPanel, 0.34f }, { ampPanel, 0.33f }, { macroPanel, 0.33f } },
            { { sequencerPanel.get(), 1.0f } },
        };
        layoutRows(soundPage, rows, viewWidth);
    }
    else if (currentPage == Page::Browser)
    {
        if (presetWorkflowPanel == nullptr || presetMetadataPanel == nullptr
            || presetBrowserPanel == nullptr || midiControllerPanel == nullptr)
            return;
        // One integrated program workflow: the quick-action/compare strip on top, the
        // program list as the dominant element, then the save-metadata and MIDI utilities
        // paired below — a preset workspace rather than forms stacked under the synth.
        std::vector<std::vector<RowItem>> rows = {
            { { presetWorkflowPanel.get(), 1.0f } },
            { { presetBrowserPanel.get(), 1.0f } },
            { { presetMetadataPanel.get(), 0.6f }, { midiControllerPanel.get(), 0.4f } },
        };
        layoutRows(browserPage, rows, viewWidth);
    }
    else if (currentPage == Page::Mod)
    {
        if (modulationOverviewPanel == nullptr)
            return;

        std::vector<std::vector<RowItem>> rows = {
            { { modulationOverviewPanel.get(), 1.0f } },
            { { oscPitchModPanel, 0.34f }, { pulseWidthModPanel, 0.33f }, { filterCutoffModPanel, 0.33f } },
            { { transModPanels[0], 0.25f }, { transModPanels[1], 0.25f },
              { transModPanels[2], 0.25f }, { transModPanels[3], 0.25f } },
            { { transModPanels[4], 0.25f }, { transModPanels[5], 0.25f },
              { transModPanels[6], 0.25f }, { transModPanels[7], 0.25f } },
        };
        layoutRows(modPage, rows, viewWidth);
    }
    else
    {
        std::vector<std::vector<RowItem>> rows = {
            { { saturationPanel, 0.25f }, { phaserPanel, 0.25f },
              { chorusPanel, 0.25f }, { eqPanel, 0.25f } },
            { { delayPanel, 0.25f }, { reverbPanel, 0.25f },
              { compressorPanel, 0.25f }, { masterFxPanel, 0.25f } },
        };
        layoutRows(fxPage, rows, viewWidth);
    }
}

void SynthAudioProcessorEditor::refreshPresetMenu()
{
    presetItems = audioProcessor.getPresetList();
    presetCombo.clear(juce::dontSendNotification);
    const auto currentFilePath = audioProcessor.getCurrentPresetFilePath();
    const auto currentName = audioProcessor.getCurrentPresetName();

    juce::String currentSection;
    for (int i = 0; i < static_cast<int>(presetItems.size()); ++i)
    {
        const auto& item = presetItems[static_cast<std::size_t>(i)];
        const auto section = presetMenuSection(item);
        if (section != currentSection)
        {
            presetCombo.addSectionHeading(section);
            currentSection = section;
        }

        presetCombo.addItem(presetMenuLabel(item), i + 1);
    }

    if (presetItems.empty())
    {
        presetCombo.setTextWhenNothingSelected("Preset browser: no presets scanned");
        if (presetBrowserPanel != nullptr)
            presetBrowserPanel->setItems(presetItems, currentFilePath, currentName);
        updateLcdPreset();
        return;
    }

    presetCombo.setTextWhenNothingSelected("Select preset");

    int selectedPresetIndex = -1;
    if (currentFilePath.isNotEmpty())
    {
        for (int i = 0; i < static_cast<int>(presetItems.size()); ++i)
        {
            const auto& item = presetItems[static_cast<std::size_t>(i)];
            if (item.valid && item.file.getFullPathName() == currentFilePath)
            {
                selectedPresetIndex = i;
                break;
            }
        }
    }

    if (selectedPresetIndex < 0)
    {
        for (int i = 0; i < static_cast<int>(presetItems.size()); ++i)
        {
            const auto& item = presetItems[static_cast<std::size_t>(i)];
            if (item.valid && item.displayName == currentName)
            {
                selectedPresetIndex = i;
                break;
            }
        }
    }

    if (selectedPresetIndex >= 0)
        presetCombo.setSelectedItemIndex(selectedPresetIndex, juce::dontSendNotification);

    if (presetBrowserPanel != nullptr)
        presetBrowserPanel->setItems(presetItems, currentFilePath, currentName);
    updateLcdPreset();
}

void SynthAudioProcessorEditor::updateLcdPreset()
{
    if (lcdDisplay == nullptr)
        return;

    const auto path = audioProcessor.getCurrentPresetFilePath();
    juce::String detail;
    int programCount = 0;
    int programIndex = 0;
    for (const auto& item : presetItems)
    {
        if (!item.valid)
            continue;
        ++programCount;
        if (path.isNotEmpty() && item.file.getFullPathName() == path)
        {
            programIndex = programCount;
            juce::StringArray parts;
            if (item.sourceLabel.isNotEmpty()) parts.add(item.sourceLabel);
            if (item.bank.isNotEmpty())        parts.add(item.bank);
            if (item.category.isNotEmpty())    parts.add(item.category);
            detail = parts.joinIntoString("   /   ");
        }
    }
    if (detail.isEmpty())
        detail = "Unsaved session";

    // Program slot reads from the real preset-list position (e.g. "012 / 128").
    juce::String program;
    if (programCount > 0)
        program = (programIndex > 0 ? juce::String(programIndex).paddedLeft('0', 3) : juce::String("---"))
                  + " / " + juce::String(programCount);

    lcdDisplay->setPreset(audioProcessor.getCurrentPresetName(), detail);
    lcdDisplay->setProgram(program);
}

void SynthAudioProcessorEditor::loadSelectedPreset()
{
    const auto index = presetCombo.getSelectedItemIndex();
    loadPresetAtIndex(index);
}

void SynthAudioProcessorEditor::loadPresetAtIndex(int itemIndex)
{
    const auto index = itemIndex;
    if (index < 0 || index >= static_cast<int>(presetItems.size()))
    {
        updateStatus("Choose a preset first");
        return;
    }

    juce::String message;
    const auto& item = presetItems[static_cast<std::size_t>(index)];
    if (!item.valid)
    {
        updateStatus("Invalid preset: " + item.validationMessage);
        return;
    }

    if (audioProcessor.loadPresetFile(item.file, message))
    {
        nameEditor.setText(audioProcessor.getCurrentPresetName(), juce::dontSendNotification);
        refreshPresetMenu();
        refreshPresetWorkflow();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
}

void SynthAudioProcessorEditor::togglePresetFavoriteAtIndex(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= static_cast<int>(presetItems.size()))
        return;

    const auto& item = presetItems[static_cast<std::size_t>(itemIndex)];
    if (!item.valid)
    {
        updateStatus("Invalid preset: " + item.validationMessage);
        return;
    }

    if (item.favoriteKey.isEmpty())
    {
        updateStatus("Preset favorite unavailable");
        return;
    }

    std::string error;
    const auto shouldFavorite = !item.favorite;
    if (!synth::setPresetFavorite(synth::defaultPresetFavoritesFile(),
                                  item.favoriteKey.toStdString(),
                                  shouldFavorite,
                                  error))
    {
        updateStatus("Favorite update failed: " + juce::String(error));
        return;
    }

    updateStatus((shouldFavorite ? "Favorited preset: " : "Removed favorite: ")
                 + (item.displayName.isNotEmpty() ? item.displayName : item.file.getFileNameWithoutExtension()));
    refreshPresetMenu();
}

void SynthAudioProcessorEditor::stepPreset(int direction)
{
    if (presetItems.empty())
    {
        updateStatus("No presets found");
        return;
    }

    auto index = presetCombo.getSelectedItemIndex();
    if (index < 0)
        index = direction > 0 ? -1 : 0;

    auto attemptsRemaining = static_cast<int>(presetItems.size());
    do
    {
        index = (index + direction + static_cast<int>(presetItems.size()))
                % static_cast<int>(presetItems.size());
        --attemptsRemaining;
    }
    while (attemptsRemaining > 0 && !presetItems[static_cast<std::size_t>(index)].valid);

    if (!presetItems[static_cast<std::size_t>(index)].valid)
    {
        updateStatus("No valid presets found");
        return;
    }

    presetCombo.setSelectedItemIndex(index, juce::dontSendNotification);
    loadSelectedPreset();
}

void SynthAudioProcessorEditor::savePresetAs()
{
    const auto presetName = nameEditor.getText().trim().isNotEmpty()
        ? nameEditor.getText().trim()
        : audioProcessor.getCurrentPresetName();
    const auto suggestedFile = audioProcessor.getUserPresetDirectory()
        .getChildFile(fileSafeName(presetName) + ".SynthiaPreset");

    fileChooser = std::make_unique<juce::FileChooser>("Save Synthia preset", suggestedFile,
                                                       "*.SynthiaPreset");
    juce::Component::SafePointer<SynthAudioProcessorEditor> safeEditor { this };
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safeEditor, presetName](const juce::FileChooser& chooser) {
                                 if (safeEditor == nullptr)
                                     return;

                                 const auto file = presetFileWithExtension(chooser.getResult());
                                 if (file == juce::File())
                                     return;

                                 juce::String message;
                                 if (safeEditor->audioProcessor.savePresetFile(file, presetName, message))
                                 {
                                     safeEditor->refreshPresetMenu();
                                     safeEditor->syncPresetMetadataPanel();
                                 }

                                 safeEditor->updateStatus(message);
                                 safeEditor->refreshPresetWorkflow();
                             });
}

void SynthAudioProcessorEditor::duplicatePreset()
{
    auto presetName = nameEditor.getText().trim();
    if (presetName.isEmpty())
        presetName = audioProcessor.getCurrentPresetName();
    if (!presetName.endsWithIgnoreCase(" Copy"))
        presetName << " Copy";

    auto directory = audioProcessor.getUserPresetDirectory();
    auto file = directory.getChildFile(fileSafeName(presetName) + ".SynthiaPreset");
    auto suffix = 2;
    while (file.existsAsFile())
        file = directory.getChildFile(fileSafeName(presetName) + "-" + juce::String(suffix++) + ".SynthiaPreset");

    juce::String message;
    if (audioProcessor.savePresetFile(file, presetName, message))
    {
        nameEditor.setText(presetName, juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::initializePreset()
{
    juce::String message;
    if (audioProcessor.initializeCurrentPreset(message))
    {
        nameEditor.setText(audioProcessor.getCurrentPresetName(), juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::resetPreset()
{
    juce::String message;
    if (audioProcessor.resetCurrentPreset(message))
    {
        nameEditor.setText(audioProcessor.getCurrentPresetName(), juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::randomizePreset()
{
    juce::String message;
    if (audioProcessor.randomizeCurrentPreset(message))
    {
        nameEditor.setText(audioProcessor.getCurrentPresetName(), juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::captureCompareSlot(int slotIndex)
{
    juce::String message;
    audioProcessor.capturePresetCompareSlot(slotIndex, message);
    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::recallCompareSlot(int slotIndex)
{
    juce::String message;
    if (audioProcessor.recallPresetCompareSlot(slotIndex, message))
    {
        nameEditor.setText(audioProcessor.getCurrentPresetName(), juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::refreshPresetWorkflow()
{
    const auto snapshot = audioProcessor.getPresetWorkflowSnapshot();
    if (hasPresetWorkflowSnapshot
        && lastPresetWorkflowSnapshot.dirty == snapshot.dirty
        && lastPresetWorkflowSnapshot.baselineValid == snapshot.baselineValid
        && lastPresetWorkflowSnapshot.compareSlotAReady == snapshot.compareSlotAReady
        && lastPresetWorkflowSnapshot.compareSlotBReady == snapshot.compareSlotBReady
        && lastPresetWorkflowSnapshot.resetAvailable == snapshot.resetAvailable
        && lastPresetWorkflowSnapshot.currentPreset == snapshot.currentPreset
        && lastPresetWorkflowSnapshot.currentPresetPath == snapshot.currentPresetPath
        && lastPresetWorkflowSnapshot.lastPresetStatus == snapshot.lastPresetStatus)
    {
        return;
    }

    lastPresetWorkflowSnapshot = snapshot;
    hasPresetWorkflowSnapshot = true;

    const auto dirtyText = snapshot.dirty ? juce::String("EDITED") : juce::String("CLEAN");
    dirtyStatePill.setText(dirtyText, juce::dontSendNotification);
    dirtyStatePill.setColour(juce::Label::backgroundColourId,
                             (snapshot.dirty ? staged : live).withAlpha(0.18f));
    dirtyStatePill.setColour(juce::Label::textColourId, snapshot.dirty ? staged : live);

    if (presetWorkflowPanel != nullptr)
        presetWorkflowPanel->refresh(snapshot);

    if (lcdDisplay != nullptr)
        lcdDisplay->setDirty(snapshot.dirty);

    // Keep the LCD preset readout honest when the preset changes underneath
    // the editor (host state restore, processor-side loads).
    updateLcdPreset();
}

const SynthAudioProcessor::PresetListItem* SynthAudioProcessorEditor::findCurrentPresetItem()
{
    currentPresetMetadataItem.reset();

    const auto currentFilePath = audioProcessor.getCurrentPresetFilePath();
    if (currentFilePath.isEmpty())
        return nullptr;

    for (const auto& item : presetItems)
    {
        if (item.file.getFullPathName() == currentFilePath)
            return &item;
    }

    currentPresetMetadataItem = audioProcessor.getPresetListItemForFile(juce::File(currentFilePath));
    return currentPresetMetadataItem.has_value() ? &(*currentPresetMetadataItem) : nullptr;
}

void SynthAudioProcessorEditor::syncPresetMetadataPanel()
{
    if (presetMetadataPanel != nullptr)
        presetMetadataPanel->syncMetadata(findCurrentPresetItem(), audioProcessor.getCurrentPresetName());
}

void SynthAudioProcessorEditor::savePresetWithMetadata(synth::PresetWriteMode mode)
{
    if (presetMetadataPanel == nullptr)
        return;

    const auto presetName = presetMetadataPanel->presetName();
    if (presetName.trim().isEmpty())
    {
        updateStatus("Preset name required");
        return;
    }

    auto options = presetMetadataPanel->writeOptions(mode);
    auto file = audioProcessor.getUserPresetDirectory()
        .getChildFile(fileSafeName(presetName) + ".SynthiaPreset");

    if (mode == synth::PresetWriteMode::OverwriteExisting)
    {
        const auto currentPresetPath = audioProcessor.getCurrentPresetFilePath();
        if (currentPresetPath.isEmpty())
        {
            updateStatus("Overwrite requires a loaded user preset");
            refreshPresetWorkflow();
            return;
        }

        file = juce::File(currentPresetPath);
        const auto* currentItem = findCurrentPresetItem();
        const auto factoryDirectory = juce::File(juce::String(synth::factoryPresetDirectory().string()));
        if ((currentItem != nullptr && currentItem->factory) || file.isAChildOf(factoryDirectory))
        {
            updateStatus("Factory presets cannot be overwritten; use Save New");
            refreshPresetWorkflow();
            return;
        }

        if (!file.existsAsFile())
        {
            updateStatus("Overwrite target no longer exists; use Save New");
            refreshPresetWorkflow();
            return;
        }
    }

    juce::String message;
    if (audioProcessor.savePresetFile(file, options, message))
    {
        nameEditor.setText(juce::String(options.metadata.displayName), juce::dontSendNotification);
        refreshPresetMenu();
        syncPresetMetadataPanel();
    }

    updateStatus(message);
    refreshPresetWorkflow();
}

void SynthAudioProcessorEditor::updateStatus(const juce::String& message)
{
    statusLabel.setText(message, juce::dontSendNotification);
}

void SynthAudioProcessorEditor::updateDiagnostics()
{
    const auto snapshot = audioProcessor.getDiagnosticsSnapshot();

    const auto sampleRate = snapshot.sampleRate > 0.0
        ? juce::String(snapshot.sampleRate, 0) + " Hz"
        : "No audio";
    const auto peakDb = snapshot.peak > 0.0001f
        ? juce::String(juce::Decibels::gainToDecibels(snapshot.peak), 1) + " dB"
        : "-inf";

    diagnosticsLabel.setText("SR " + sampleRate
                             + "   Block " + juce::String(snapshot.blockSize)
                             + "   Peak " + peakDb
                             + "   MIDI " + juce::String(snapshot.midiEvents)
                             + "   Invalid " + juce::String(snapshot.invalidSamples)
                             + "   " + snapshot.architecture,
                             juce::dontSendNotification);

    voicesValue.setText(juce::String(snapshot.activeVoices) + "/" + juce::String(snapshot.patchCost.maxActiveVoices),
                        juce::dontSendNotification);

    const auto percent = snapshot.patchCost.loadPercent;
    costValue.setText(percent <= 100 ? juce::String(percent) + "%" : ">100%", juce::dontSendNotification);
    costValue.setColour(juce::Label::textColourId, snapshot.patchCost.high ? warn
                                            : (snapshot.patchCost.elevated ? staged : text));

    const auto clip = snapshot.peak >= 0.98f || snapshot.invalidSamples > lastInvalidSamples;
    lastInvalidSamples = snapshot.invalidSamples;
    meter->setLevel(snapshot.peak, clip);

    // Mirror the most performance-relevant live state into the LCD hub (all real snapshot data).
    if (lcdDisplay != nullptr)
    {
        const auto srShort = snapshot.sampleRate > 0.0
            ? juce::String(snapshot.sampleRate / 1000.0, 1) + "k"
            : juce::String("--");
        lcdDisplay->setDiagnostics("VOICES " + juce::String(snapshot.activeVoices) + "/"
                                   + juce::String(snapshot.patchCost.maxActiveVoices)
                                   + "   LOAD " + (percent <= 100 ? juce::String(percent) + "%" : juce::String(">100%"))
                                   + "   SR " + srShort);
    }
}

void SynthAudioProcessorEditor::wireModulationTargets()
{
    modTargetSliders.clear();
    for (auto& panel : soundPanels)
        panel->collectModTargets(modTargetSliders);
    if (filterPanel != nullptr)
        filterPanel->collectModTargets(modTargetSliders);

    for (auto* target : modTargetSliders)
    {
        target->onModSourceDropped = [this, target](const juce::String& sourceId) {
            handleModSourceDrop(*target, sourceId);
        };
        target->onDepthEdited = [this, target](int slotNumber, float depth) {
            if (const auto* destination = target->destinationInfo())
                handleModDepthEdit(slotNumber, juce::String(destination->id), depth);
        };
        target->onSlotCleared = [this](int slotNumber) { handleModSlotClear(slotNumber); };
    }
}

void SynthAudioProcessorEditor::handleModSourceDrop(ModTargetSlider& target, const juce::String& sourceId)
{
    const auto* destination = target.destinationInfo();
    const auto* source = synth::findModulationSourceInfo(sourceId.toStdString());
    if (destination == nullptr || source == nullptr)
        return;

    const auto view = audioProcessor.getModulationRouteView();
    for (const auto& route : view.activeRoutes)
    {
        if (route.destinationId == destination->id && route.sourceId == source->id)
        {
            updateStatus(juce::String(source->label) + " already routed in slot "
                         + juce::String(route.slotNumber) + " - alt-drag the knob to set depth");
            return;
        }
    }

    int freeSlot = 0;
    for (const auto& slot : view.slots)
    {
        if (!slot.enabled)
        {
            freeSlot = slot.slotNumber;
            break;
        }
    }
    if (freeSlot == 0)
    {
        updateStatus("All 8 TransMod slots are in use - clear one to add this route");
        return;
    }

    synth::ModulationRouteWriteRequest request;
    request.slotNumber = freeSlot;
    request.sourceId = source->id;
    request.scalerId = "none";
    request.destinationId = destination->id;
    request.depth = 0.25f * destination->maximumDepth;

    juce::String message;
    if (!audioProcessor.writeModulationRoute(request, message))
    {
        updateStatus("Modulation route failed: " + message);
        return;
    }

    updateStatus(juce::String(source->label) + " -> " + juce::String(destination->label)
                 + " routed in slot " + juce::String(freeSlot)
                 + " - alt-drag to set depth, right-click to clear");
    if (lcdDisplay != nullptr)
        lcdDisplay->setTouchReadout(juce::String(source->label) + " -> "
                                    + juce::String(destination->label));
    refreshModulationVisuals();
    if (modulationOverviewPanel != nullptr)
        modulationOverviewPanel->refresh();
}

void SynthAudioProcessorEditor::handleModDepthEdit(int slotNumber, const juce::String& destinationId,
                                                   float depth)
{
    const auto view = audioProcessor.getModulationRouteView();
    const synth::ModulationSlotSummary* slot = nullptr;
    for (const auto& candidate : view.slots)
    {
        if (candidate.slotNumber == slotNumber)
        {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr || slot->sourceId.empty() || slot->sourceId == "none")
        return;

    synth::ModulationRouteWriteRequest request;
    request.slotNumber = slotNumber;
    request.sourceId = slot->sourceId;
    request.scalerId = slot->scalerId.empty() ? "none" : slot->scalerId;
    request.destinationId = destinationId.toStdString();
    request.depth = depth;

    juce::String message;
    if (!audioProcessor.writeModulationRoute(request, message))
        return;

    const auto* destination = synth::findModulationDestinationInfo(request.destinationId);
    const auto unit = destination != nullptr && destination->unit == "semitones" ? " st"
        : destination != nullptr && destination->unit == "dB"                    ? " dB"
                                                                                 : "";
    if (lcdDisplay != nullptr)
        lcdDisplay->setTouchReadout("Slot " + juce::String(slotNumber) + " depth  =  "
                                    + juce::String(depth, 2) + unit);
    refreshModulationVisuals();
}

void SynthAudioProcessorEditor::handleModSlotClear(int slotNumber)
{
    juce::String message;
    if (!audioProcessor.clearModulationSlot(slotNumber, message))
    {
        updateStatus("Clear failed: " + message);
        return;
    }

    updateStatus("TransMod slot " + juce::String(slotNumber) + " cleared");
    refreshModulationVisuals();
    if (modulationOverviewPanel != nullptr)
        modulationOverviewPanel->refresh();
}

void SynthAudioProcessorEditor::refreshModulationVisuals()
{
    const auto view = audioProcessor.getModulationRouteView();
    const auto visual = audioProcessor.getUiVisualSnapshot();

    if (modSourcesPanel != nullptr)
        modSourcesPanel->setRouteView(view);

    for (auto* target : modTargetSliders)
    {
        const auto* destination = target->destinationInfo();
        if (destination == nullptr)
            continue;

        std::vector<ModTargetSlider::RouteDisplay> displays;
        for (const auto& route : view.activeRoutes)
        {
            if (route.destinationId != destination->id)
                continue;

            ModTargetSlider::RouteDisplay display;
            display.slotNumber = route.slotNumber;
            display.depth = route.depth;
            display.lfoSource = route.source == synth::ModSource::Lfo;
            const auto* source = synth::findModulationSourceInfo(route.source);
            display.bipolarSource = source != nullptr
                && source->polarity == synth::ModulationPolarity::Bipolar;
            display.label = juce::String(source != nullptr ? source->label : "Source")
                + " -> " + juce::String(destination->label);
            displays.push_back(std::move(display));
        }

        target->setRouteDisplays(std::move(displays));
        target->setLiveLfo(visual.lfoVoiceActive, visual.lfoValue);
    }
}

void SynthAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    showSliderReadout(event);
}

void SynthAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    showSliderReadout(event);
}

void SynthAudioProcessorEditor::showSliderReadout(const juce::MouseEvent& event)
{
    if (lcdDisplay == nullptr)
        return;

    if (event.eventComponent == nullptr)
        return;

    const auto parameterId = event.eventComponent->getProperties()[readoutParameterIdProperty].toString();
    if (parameterId.isNotEmpty())
    {
        const auto parameterKey = parameterId.toStdString();
        const auto* spec = synth::findParameterSpec(parameterKey);
        const auto* rawValue = audioProcessor.getValueTreeState().getRawParameterValue(parameterId);
        if (spec != nullptr && rawValue != nullptr)
        {
            lcdDisplay->setTouchReadout(juce::String(spec->name) + "  =  "
                                        + formatValue(*spec, rawValue->load()));
            return;
        }
    }

    const auto name = event.eventComponent->getName();
    if (name.isNotEmpty())
    {
        lcdDisplay->setTouchReadout(name);
    }
}

void SynthAudioProcessorEditor::timerCallback()
{
    updateDiagnostics();
    refreshPresetWorkflow();
    if (lcdDisplay != nullptr)
        lcdDisplay->tickTouchReadout();
    if (midiControllerPanel != nullptr)
        midiControllerPanel->refresh();

    // Refresh module power dots/dim for the visible page only (keeps hidden tabs idle).
    if (currentPage == Page::Fx)
    {
        for (auto& panel : fxPanels)
            panel->syncEnabledState();
    }
    else if (currentPage == Page::Mod)
    {
        if (modulationOverviewPanel != nullptr)
            modulationOverviewPanel->refresh();
        for (auto* panel : transModPanels)
            if (panel != nullptr)
                panel->syncEnabledState();
    }
    else if (currentPage == Page::Sound)
    {
        for (auto& panel : slotPanels)
            if (panel != nullptr)
                panel->syncEnabledState();
        if (filterPanel != nullptr)
            filterPanel->syncEnabledState();
        refreshModulationVisuals();
    }
}
