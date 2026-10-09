#include "ModulationRouteModel.h"

#include "../plugin/ParameterRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace synth
{
namespace
{
bool finiteNonZero(float value) noexcept
{
    return std::isfinite(value) && std::abs(value) > 0.000001f;
}

ModSource sanitizeSource(ModSource source) noexcept
{
    const auto value = static_cast<int>(source);
    if (value < static_cast<int>(ModSource::None) || value > static_cast<int>(ModSource::StepVelocity))
        return ModSource::None;

    return source;
}

float depthForDestination(const TransModSlotParameters& slot,
                          const ModulationDestinationInfo& info) noexcept
{
    switch (info.destination)
    {
        case ModulationDestination::OscPitch:
            return std::clamp(slot.oscPitchSemitones, -48.0f, 48.0f);
        case ModulationDestination::PulseWidth:
            return std::clamp(slot.pulseWidth, -1.0f, 1.0f);
        case ModulationDestination::FilterCutoff:
            return std::clamp(slot.filterCutoffSemitones, -72.0f, 72.0f)
                + std::clamp(slot.depth, -1.0f, 1.0f) * 72.0f;
        case ModulationDestination::AmpLevel:
            return std::clamp(slot.ampLevelDb, -24.0f, 24.0f);
        case ModulationDestination::Pan:
            return std::clamp(slot.pan, -1.0f, 1.0f);
        case ModulationDestination::Native:
            return std::clamp(slot.nativeDepths[static_cast<std::size_t>(info.nativeIndex)], info.minimumDepth, info.maximumDepth);
    }

    return 0.0f;
}

bool hasDepthForDestination(const TransModSlotParameters& slot,
                            const ModulationDestinationInfo& info) noexcept
{
    switch (info.destination)
    {
        case ModulationDestination::OscPitch:
            return finiteNonZero(slot.oscPitchSemitones);
        case ModulationDestination::PulseWidth:
            return finiteNonZero(slot.pulseWidth);
        case ModulationDestination::FilterCutoff:
            return finiteNonZero(slot.filterCutoffSemitones) || finiteNonZero(slot.depth);
        case ModulationDestination::AmpLevel:
            return finiteNonZero(slot.ampLevelDb);
        case ModulationDestination::Pan:
            return finiteNonZero(slot.pan);
        case ModulationDestination::Native:
            return finiteNonZero(slot.nativeDepths[static_cast<std::size_t>(info.nativeIndex)]);
    }

    return false;
}

std::vector<ModulationSourceInfo> buildSourceCatalog()
{
    return {
        { ModSource::None, "none", "None", ModulationPolarity::Unipolar, ModulationScope::Global, ModulationUpdateRate::Control, {} },
        { ModSource::Lfo, "lfo.1", "LFO 1", ModulationPolarity::Bipolar, ModulationScope::Voice,
          ModulationUpdateRate::Audio, "lfo.shape" },
        { ModSource::Ramp, "ramp", "Ramp", ModulationPolarity::Unipolar, ModulationScope::Voice,
          ModulationUpdateRate::Audio, "ramp.enabled" },
        { ModSource::ModEnv, "mod_env", "Mod Env", ModulationPolarity::Unipolar, ModulationScope::Voice,
          ModulationUpdateRate::Audio, "mod_env.attack_ms" },
        { ModSource::AmpEnv, "amp_env", "Amp Env", ModulationPolarity::Unipolar, ModulationScope::Voice,
          ModulationUpdateRate::Audio, "layer.1.amp_env.attack_ms" },
        { ModSource::Keytrack, "keytrack", "Keytrack", ModulationPolarity::Bipolar, ModulationScope::Voice,
          ModulationUpdateRate::Note, "filter_control.keytrack" },
        { ModSource::Velocity, "velocity", "Velocity", ModulationPolarity::Unipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::VelocityGlide, "velocity_glide", "Velocity Glide", ModulationPolarity::Unipolar,
          ModulationScope::Voice, ModulationUpdateRate::Audio, "voice.velocity_glide_ms" },
        { ModSource::PitchBend, "pitch_bend", "Pitch Bend", ModulationPolarity::Bipolar, ModulationScope::Global, ModulationUpdateRate::Control, {} },
        { ModSource::ModWheel, "mod_wheel", "Mod Wheel", ModulationPolarity::Unipolar, ModulationScope::Global, ModulationUpdateRate::Control, {} },
        { ModSource::Aftertouch, "aftertouch", "Aftertouch", ModulationPolarity::Unipolar, ModulationScope::Global, ModulationUpdateRate::Control, {} },
        { ModSource::VoiceUni, "voice_uni", "Voice Index +", ModulationPolarity::Unipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::VoiceBi, "voice_bi", "Voice Index +/-", ModulationPolarity::Bipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::UnisonUni, "unison_uni", "Unison Index +", ModulationPolarity::Unipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::UnisonBi, "unison_bi", "Unison Index +/-", ModulationPolarity::Bipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::RandomOnNote, "random_on_note", "Random", ModulationPolarity::Bipolar, ModulationScope::Voice, ModulationUpdateRate::Note, {} },
        { ModSource::Macro1, "macro.motion", "Macro Motion", ModulationPolarity::Unipolar,
          ModulationScope::Global, ModulationUpdateRate::Control, "macro.motion" },
        { ModSource::Macro2, "macro.width", "Macro Width", ModulationPolarity::Unipolar,
          ModulationScope::Global, ModulationUpdateRate::Control, "macro.width" },
        { ModSource::Macro3, "macro.drive", "Macro Drive", ModulationPolarity::Unipolar,
          ModulationScope::Global, ModulationUpdateRate::Control, "macro.drive" },
        { ModSource::Macro4, "macro.space", "Macro Space", ModulationPolarity::Unipolar,
          ModulationScope::Global, ModulationUpdateRate::Control, "macro.space" },
        { ModSource::ModEnv2, "mod_env.2", "Mod Env 2", ModulationPolarity::Unipolar, ModulationScope::Voice, ModulationUpdateRate::Audio, "mod_env.2.attack_ms" },
        { ModSource::Lfo2, "lfo.2", "LFO 2", ModulationPolarity::Bipolar, ModulationScope::Voice, ModulationUpdateRate::Audio, "lfo.2.shape" },
        { ModSource::AmpEnv2, "amp_env.2", "Amp Env B", ModulationPolarity::Unipolar, ModulationScope::Voice, ModulationUpdateRate::Audio, "layer.2.amp_env.attack_ms" },
        { ModSource::StepVelocity, "step_velocity", "Step Velocity", ModulationPolarity::Unipolar, ModulationScope::Global, ModulationUpdateRate::Audio, "arp.enabled" }
    };
}

std::vector<ModulationDestinationInfo> buildDestinationCatalog()
{
    std::vector<ModulationDestinationInfo> destinations {
        { ModulationDestination::OscPitch, "osc.pitch", "Osc Pitch", {}, "osc_pitch_semitones", "semitones", -48.0f, 48.0f },
        { ModulationDestination::FilterCutoff, "filter.cutoff", "Filter Cutoff", {}, "filter_cutoff_semitones", "semitones", -144.0f, 144.0f },
        { ModulationDestination::AmpLevel, "amp.level", "Amp Level", {}, "amp_level_db", "dB", -24.0f, 24.0f },
        { ModulationDestination::Pan, "amp.pan", "Pan", {}, "pan", "normalized", -1.0f, 1.0f }
    };
    auto append = [&destinations](std::string target, std::string label, float maximum, std::string unit) {
        const auto index = static_cast<int>(destinations.size()) - 4;
        destinations.push_back({ ModulationDestination::Native, target, std::move(label), target,
                                 "native." + target, std::move(unit), -maximum, maximum, index });
    };
    for (int layer = 1; layer <= layerCount; ++layer)
        for (int osc = 1; osc <= oscillatorSlotsPerLayer; ++osc)
        {
            const auto prefix = "layer." + std::to_string(layer) + ".osc." + std::to_string(osc) + ".";
            const auto name = std::string(1, static_cast<char>('A' + layer - 1)) + std::to_string(osc);
            append(prefix + "note", name + " Pitch", 48.0f, "semitones");
            append(prefix + "level", name + " Volume", 24.0f, "dB");
            append(prefix + "pan", name + " Pan", 1.0f, "normalized");
            append(prefix + "detune", name + " Detune", 1.0f, "normalized");
            append(prefix + "phase_degrees", name + " Phase", 180.0f, "degrees");
        }
    for (int layer = 1; layer <= layerCount; ++layer)
    {
        const auto prefix = "layer." + std::to_string(layer) + ".filter.";
        const auto name = "Filter " + std::string(1, static_cast<char>('A' + layer - 1));
        append(prefix + "cutoff_semitones", name + " Cutoff", 72.0f, "semitones");
        append(prefix + "resonance", name + " Resonance", 1.0f, "normalized");
        append(prefix + "drive", name + " Drive", 1.0f, "normalized");
    }
    for (int layer = 1; layer <= layerCount; ++layer)
    {
        const auto prefix = "layer." + std::to_string(layer) + ".";
        const auto name = "Part " + std::string(1, static_cast<char>('A' + layer - 1));
        append(prefix + "level_db", name + " Volume", 24.0f, "dB");
        append(prefix + "pan", name + " Pan", 1.0f, "normalized");
    }
    for (int lfo = 1; lfo <= 2; ++lfo)
    {
        const auto prefix = lfo == 1 ? std::string("lfo.") : std::string("lfo.2.");
        const auto name = "LFO " + std::to_string(lfo);
        append(prefix + "rate_hz", name + " Rate", 40.0f, "Hz");
        append(prefix + "gain", name + " Gain", 1.0f, "normalized");
        append(prefix + "offset", name + " Offset", 1.0f, "normalized");
    }
    append("fx.phaser_center_hz", "Phaser Center Frequency", 20000.0f, "Hz");
    append("filter_control.resonance", "Resonance A+B", 1.0f, "normalized");
    return destinations;
}

bool validSlotNumber(int slotNumber) noexcept
{
    return slotNumber >= 1 && slotNumber <= transModSlotCount;
}

std::string transModParameterId(int slotNumber, const char* suffix)
{
    if (!validSlotNumber(slotNumber))
        return {};

    return "transmod." + std::to_string(slotNumber) + "." + suffix;
}

float clampedRegisteredParameterValue(const std::string& parameterId, float value) noexcept
{
    if (const auto* spec = findParameterSpec(parameterId))
        return clampPhysicalParameterValue(*spec, value);

    return value;
}

void addEdit(std::vector<ModulationRouteParameterEdit>& edits,
             std::string parameterId,
             float value)
{
    edits.push_back({ std::move(parameterId), value });
}

void addDepthClearEdits(std::vector<ModulationRouteParameterEdit>& edits, int slotNumber)
{
    addEdit(edits, transModParameterId(slotNumber, "depth"), 0.0f);
    for (const auto& destination : modulationDestinationCatalog())
        addEdit(edits, transModDepthParameterId(slotNumber, destination), 0.0f);
    for (int route = 1; route <= 2; ++route)
    {
        const auto prefix = "transmod." + std::to_string(slotNumber) + ".route." + std::to_string(route) + ".";
        addEdit(edits, prefix + "destination", 0.0f);
        addEdit(edits, prefix + "amount", 0.0f);
    }
}
} // namespace

const std::vector<ModulationSourceInfo>& modulationSourceCatalog()
{
    static const auto catalog = buildSourceCatalog();
    return catalog;
}

const std::vector<ModulationDestinationInfo>& modulationDestinationCatalog()
{
    static const auto catalog = buildDestinationCatalog();
    return catalog;
}

const ModulationSourceInfo* findModulationSourceInfo(ModSource source)
{
    source = sanitizeSource(source);
    const auto& catalog = modulationSourceCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [source](const auto& info) {
        return info.source == source;
    });
    return found == catalog.end() ? nullptr : &*found;
}

const ModulationSourceInfo* findModulationSourceInfo(const std::string& id)
{
    const auto& catalog = modulationSourceCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&id](const auto& info) {
        return info.id == id;
    });
    return found == catalog.end() ? nullptr : &*found;
}

const ModulationDestinationInfo* findModulationDestinationInfo(ModulationDestination destination)
{
    const auto& catalog = modulationDestinationCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [destination](const auto& info) {
        return info.destination == destination;
    });
    return found == catalog.end() ? nullptr : &*found;
}

const ModulationDestinationInfo* findModulationDestinationInfo(const std::string& id)
{
    const auto& catalog = modulationDestinationCatalog();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [&id](const auto& info) {
        return info.id == id;
    });
    return found == catalog.end() ? nullptr : &*found;
}

std::string transModDepthParameterId(int slotNumber, const ModulationDestinationInfo& destination)
{
    if (slotNumber < 1 || slotNumber > transModSlotCount)
        return {};

    return "transmod." + std::to_string(slotNumber) + "." + destination.depthSuffix;
}

ModulationRouteView buildModulationRouteView(const TransModParameters& transMod)
{
    ModulationRouteView view;
    view.slots.reserve(transModSlotCount);
    view.activeRoutes.reserve(transModSlotCount * modulationDestinationCatalog().size());

    for (int slotIndex = 0; slotIndex < transModSlotCount; ++slotIndex)
    {
        const auto slotNumber = slotIndex + 1;
        const auto& slot = transMod.slots[static_cast<std::size_t>(slotIndex)];
        const auto source = sanitizeSource(slot.source);
        const auto scaler = sanitizeSource(slot.scaler);
        const auto* sourceInfo = findModulationSourceInfo(source);
        const auto* scalerInfo = findModulationSourceInfo(scaler);

        ModulationSlotSummary slotSummary;
        slotSummary.slotNumber = slotNumber;
        slotSummary.enabled = slot.enabled && source != ModSource::None;
        slotSummary.source = source;
        slotSummary.scaler = scaler;
        slotSummary.sourceId = sourceInfo != nullptr ? sourceInfo->id : "none";
        slotSummary.scalerId = scalerInfo != nullptr ? scalerInfo->id : "none";

        if (slotSummary.enabled)
        {
            for (const auto& destination : modulationDestinationCatalog())
            {
                const auto depth = depthForDestination(slot, destination);
                if (!hasDepthForDestination(slot, destination))
                    continue;

                ModulationRouteSummary route;
                route.slotNumber = slotNumber;
                route.source = source;
                route.scaler = scaler;
                route.destination = destination.destination;
                route.sourceId = slotSummary.sourceId;
                route.scalerId = slotSummary.scalerId;
                route.destinationId = destination.id;
                route.depthParameterId = transModDepthParameterId(slotNumber, destination);
                route.depthParameterIds.push_back(route.depthParameterId);
                if (destination.destination == ModulationDestination::FilterCutoff
                    && finiteNonZero(slot.depth))
                {
                    const auto legacyDepthId = "transmod." + std::to_string(slotNumber) + ".depth";
                    route.depthParameterIds.push_back(legacyDepthId);
                    if (!finiteNonZero(slot.filterCutoffSemitones))
                        route.depthParameterId = legacyDepthId;
                }
                route.depth = std::clamp(depth, destination.minimumDepth, destination.maximumDepth);
                route.enabled = true;

                slotSummary.routes.push_back(route);
                view.activeRoutes.push_back(std::move(route));
            }
        }

        view.slots.push_back(std::move(slotSummary));
    }

    return view;
}

ModulationRouteWriteResult buildModulationRouteWrite(const ModulationRouteWriteRequest& request)
{
    ModulationRouteWriteResult result;
    if (!validSlotNumber(request.slotNumber))
    {
        result.message = "modulation slot must be 1 through 8";
        return result;
    }

    const auto* source = findModulationSourceInfo(request.sourceId);
    if (source == nullptr || source->source == ModSource::None)
    {
        result.message = "modulation source is unknown or None";
        return result;
    }

    const auto scalerId = request.scalerId.empty() ? std::string("none") : request.scalerId;
    const auto* scaler = findModulationSourceInfo(scalerId);
    if (scaler == nullptr)
    {
        result.message = "modulation scaler is unknown";
        return result;
    }

    const auto* destination = findModulationDestinationInfo(request.destinationId);
    if (destination == nullptr)
    {
        result.message = "modulation destination is unknown";
        return result;
    }

    if (!std::isfinite(request.depth) || std::abs(request.depth) <= 0.000001f)
    {
        result.message = "modulation depth must be finite and nonzero";
        return result;
    }

    const auto slotNumber = request.slotNumber;
    const auto depthParameterId = transModDepthParameterId(slotNumber, *destination);
    const auto depth = clampedRegisteredParameterValue(depthParameterId, request.depth);

    addEdit(result.edits, transModParameterId(slotNumber, "enabled"), 1.0f);
    addEdit(result.edits, transModParameterId(slotNumber, "source"),
            static_cast<float>(static_cast<int>(source->source)));
    addEdit(result.edits, transModParameterId(slotNumber, "scaler"),
            static_cast<float>(static_cast<int>(scaler->source)));
    if (request.replaceExistingDestinations)
        addDepthClearEdits(result.edits, slotNumber);
    addEdit(result.edits, depthParameterId, depth);

    result.ok = true;
    result.message = "modulation route write compiled";
    return result;
}

ModulationRouteWriteResult buildModulationSlotClear(int slotNumber)
{
    ModulationRouteWriteResult result;
    if (!validSlotNumber(slotNumber))
    {
        result.message = "modulation slot must be 1 through 8";
        return result;
    }

    addEdit(result.edits, transModParameterId(slotNumber, "enabled"), 0.0f);
    addEdit(result.edits, transModParameterId(slotNumber, "source"), 0.0f);
    addEdit(result.edits, transModParameterId(slotNumber, "scaler"), 0.0f);
    addDepthClearEdits(result.edits, slotNumber);

    result.ok = true;
    result.message = "modulation slot clear compiled";
    return result;
}
} // namespace synth
