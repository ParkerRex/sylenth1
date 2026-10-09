#pragma once

#include "../dsp/SynthParameters.h"

#include <string>
#include <vector>

namespace synth
{
enum class ModulationPolarity
{
    Unipolar,
    Bipolar
};

enum class ModulationScope
{
    Voice,
    Global
};

enum class ModulationUpdateRate
{
    Audio,
    Control,
    Note
};

enum class ModulationDestination
{
    OscPitch,
    PulseWidth,
    FilterCutoff,
    AmpLevel,
    Pan,
    Native
};

struct ModulationSourceInfo
{
    ModSource source = ModSource::None;
    std::string id;
    std::string label;
    ModulationPolarity polarity = ModulationPolarity::Unipolar;
    ModulationScope scope = ModulationScope::Voice;
    ModulationUpdateRate updateRate = ModulationUpdateRate::Control;
    std::string parameterId;
};

struct ModulationDestinationInfo
{
    ModulationDestination destination = ModulationDestination::OscPitch;
    std::string id;
    std::string label;
    std::string targetParameterId;
    std::string depthSuffix;
    std::string unit;
    float minimumDepth = -1.0f;
    float maximumDepth = 1.0f;
    int nativeIndex = -1;
};

struct ModulationRouteSummary
{
    int slotNumber = 0;
    ModSource source = ModSource::None;
    ModSource scaler = ModSource::None;
    ModulationDestination destination = ModulationDestination::OscPitch;
    std::string sourceId;
    std::string scalerId;
    std::string destinationId;
    std::string depthParameterId;
    std::vector<std::string> depthParameterIds;
    float depth = 0.0f;
    bool enabled = false;
};

struct ModulationSlotSummary
{
    int slotNumber = 0;
    bool enabled = false;
    ModSource source = ModSource::None;
    ModSource scaler = ModSource::None;
    std::string sourceId;
    std::string scalerId;
    std::vector<ModulationRouteSummary> routes;
};

struct ModulationRouteView
{
    std::vector<ModulationSlotSummary> slots;
    std::vector<ModulationRouteSummary> activeRoutes;
};

struct ModulationRouteParameterEdit
{
    std::string parameterId;
    float value = 0.0f;
};

struct ModulationRouteWriteRequest
{
    int slotNumber = 0;
    std::string sourceId;
    std::string scalerId = "none";
    std::string destinationId;
    float depth = 0.0f;
    bool replaceExistingDestinations = true;
};

struct ModulationRouteWriteResult
{
    bool ok = false;
    std::string message;
    std::vector<ModulationRouteParameterEdit> edits;
};

const std::vector<ModulationSourceInfo>& modulationSourceCatalog();
const std::vector<ModulationDestinationInfo>& modulationDestinationCatalog();

const ModulationSourceInfo* findModulationSourceInfo(ModSource source);
const ModulationSourceInfo* findModulationSourceInfo(const std::string& id);
const ModulationDestinationInfo* findModulationDestinationInfo(ModulationDestination destination);
const ModulationDestinationInfo* findModulationDestinationInfo(const std::string& id);

std::string transModDepthParameterId(int slotNumber, const ModulationDestinationInfo& destination);
ModulationRouteView buildModulationRouteView(const TransModParameters& transMod);
ModulationRouteWriteResult buildModulationRouteWrite(const ModulationRouteWriteRequest& request);
ModulationRouteWriteResult buildModulationSlotClear(int slotNumber);
} // namespace synth
