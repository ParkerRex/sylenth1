#include "PresetValidator.h"

#include "../dsp/SynthParameters.h"
#include "../plugin/ParameterRegistry.h"
#include "../modulation/ModulationRouteModel.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <system_error>

namespace synth
{
namespace
{
juce::File juceFileForPath(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return juce::File { juce::String(path.lexically_normal().string()) };

    std::error_code error;
    auto absolutePath = std::filesystem::current_path(error);
    if (!error)
        absolutePath /= path;
    else
        absolutePath = path;

    return juce::File { juce::String(absolutePath.lexically_normal().string()) };
}

bool hasProperty(const juce::DynamicObject& object, const char* name)
{
    return object.hasProperty(juce::Identifier(name));
}

const juce::var getProperty(const juce::DynamicObject& object, const char* name)
{
    return object.getProperty(juce::Identifier(name));
}

bool isNumber(const juce::var& value)
{
    return value.isDouble() || value.isInt() || value.isInt64();
}

bool isAllowedChoice(const ParameterSpec& spec, const juce::var& value)
{
    if (!value.isString())
        return false;

    const auto selected = value.toString().toStdString();
    return std::find(spec.choices.begin(), spec.choices.end(), selected) != spec.choices.end();
}

bool isAllowedModDestination(const std::string& targetId)
{
    return targetId == "osc.pitch_semitones"
        || targetId == "filter.cutoff_semitones"
        || targetId == "amp.level_db"
        || targetId == "amp.pan"
        || std::any_of(modulationDestinationCatalog().begin(), modulationDestinationCatalog().end(), [&targetId](const auto& destination) { return destination.nativeIndex >= 0 && destination.targetParameterId == targetId; });
}

bool isDepthInRange(const std::string& targetId, const std::string& depthDomain, float value)
{
    if (depthDomain == "Normalized")
        return value >= -1.0f && value <= 1.0f;

    if (targetId == "osc.pitch_semitones")
        return value >= -48.0f && value <= 48.0f;
    if (targetId == "filter.cutoff_semitones")
        return value >= -72.0f && value <= 72.0f;
    if (targetId == "amp.level_db")
        return value >= -24.0f && value <= 24.0f;
    if (targetId == "amp.pan")
        return value >= -1.0f && value <= 1.0f;

    for (const auto& destination : modulationDestinationCatalog())
        if (destination.targetParameterId == targetId && destination.nativeIndex >= 0)
            return std::isfinite(value) && value >= destination.minimumDepth && value <= destination.maximumDepth;
    return false;
}

bool isAllowedBrowserSource(const juce::var& source)
{
    if (!source.isString())
        return false;

    const auto value = source.toString();
    return value == "factory"
        || value == "user"
        || value == "ai_generated";
}

std::string lowercaseExtension(std::filesystem::path path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

bool isPresetFilePath(const std::filesystem::path& path)
{
    return lowercaseExtension(path) == ".synthiapreset";
}

void validateTags(PresetValidationResult& result, const juce::var& tags);

const juce::DynamicObject* presetPayloadObject(PresetValidationResult& result, const juce::DynamicObject& root)
{
    const auto fileType = root.getProperty(juce::Identifier("fileType"));
    if (fileType.isVoid())
    {
        result.errors.push_back("fileType must be SynthiaPreset");
        return nullptr;
    }

    if (!fileType.isVoid())
    {
        if (!fileType.isString() || fileType.toString() != "SynthiaPreset")
        {
            result.errors.push_back("fileType must be SynthiaPreset");
            return nullptr;
        }

        const auto presetName = root.getProperty(juce::Identifier("presetName"));
        if (!presetName.isString())
            result.errors.push_back("presetName must be a string");

        const auto presetAuthor = root.getProperty(juce::Identifier("presetAuthor"));
        if (!presetAuthor.isString())
            result.errors.push_back("presetAuthor must be a string");

        const auto presetDescription = root.getProperty(juce::Identifier("presetDescription"));
        if (!presetDescription.isString())
            result.errors.push_back("presetDescription must be a string");

        const auto product = root.getProperty(juce::Identifier("product"));
        if (!product.isString() || product.toString() != "Synthia")
            result.errors.push_back("product must be Synthia");

        const auto version = root.getProperty(juce::Identifier("version"));
        if (!version.isInt() || static_cast<int>(version) < 1)
            result.errors.push_back("version must be an integer >= 1");

        const auto tags = root.getProperty(juce::Identifier("tags"));
        if (!tags.isVoid())
            validateTags(result, tags);

        const auto preview = root.getProperty(juce::Identifier("preview"));
        if (!preview.isVoid() && !preview.isObject())
            result.errors.push_back("preview must be an object");

        const auto preset = root.getProperty(juce::Identifier("preset"));
        if (!preset.isObject())
        {
            result.errors.push_back("preset must be an object");
            return nullptr;
        }

        return preset.getDynamicObject();
    }

    return nullptr;
}

void validateParameterValue(PresetValidationResult& result, const ParameterSpec& spec, const juce::var& value)
{
    if (spec.kind == ParameterKind::Float)
    {
        if (!isNumber(value))
        {
            result.errors.push_back(spec.id + " must be numeric");
            return;
        }

        const auto numeric = static_cast<float>(static_cast<double>(value));
        if (numeric < spec.minimum || numeric > spec.maximum)
            result.errors.push_back(spec.id + " is outside allowed range");
    }
    else if (spec.kind == ParameterKind::Bool)
    {
        if (!value.isBool())
            result.errors.push_back(spec.id + " must be boolean");
    }
    else
    {
        if (!value.isString())
        {
            result.errors.push_back(spec.id + " must be a choice string");
            return;
        }

        const auto selected = value.toString().toStdString();
        if (std::find(spec.choices.begin(), spec.choices.end(), selected) == spec.choices.end())
            result.errors.push_back(spec.id + " has unknown choice '" + selected + "'");
    }
}

void validateTags(PresetValidationResult& result, const juce::var& tags)
{
    if (!tags.isArray())
    {
        result.errors.push_back("tags must be an array");
        return;
    }

    const auto* tagArray = tags.getArray();
    if (tagArray == nullptr)
        return;

    for (const auto& tag : *tagArray)
    {
        if (!tag.isString())
            result.errors.push_back("tags entries must be strings");
    }
}

void validateBrowserMetadata(PresetValidationResult& result, const juce::DynamicObject& metadata)
{
    const auto browser = metadata.getProperty(juce::Identifier("browser"));
    if (browser.isVoid())
        return;

    if (!browser.isObject())
    {
        result.errors.push_back("metadata.browser must be an object");
        return;
    }

    const auto* browserObject = browser.getDynamicObject();
    if (browserObject == nullptr)
    {
        result.errors.push_back("metadata.browser object unavailable");
        return;
    }

    const auto bank = browserObject->getProperty(juce::Identifier("bank"));
    if (!bank.isVoid() && !bank.isString())
        result.errors.push_back("metadata.browser.bank must be a string");

    const auto category = browserObject->getProperty(juce::Identifier("category"));
    if (!category.isVoid() && !category.isString())
        result.errors.push_back("metadata.browser.category must be a string");

    const auto source = browserObject->getProperty(juce::Identifier("source"));
    if (!source.isVoid() && !isAllowedBrowserSource(source))
        result.errors.push_back("metadata.browser.source must be factory, user, or ai_generated");
}

void validateMetadata(PresetValidationResult& result, const juce::var& metadata)
{
    if (metadata.isVoid())
        return;

    if (!metadata.isObject())
    {
        result.errors.push_back("metadata must be an object");
        return;
    }

    const auto* metadataObject = metadata.getDynamicObject();
    if (metadataObject == nullptr)
    {
        result.errors.push_back("metadata object unavailable");
        return;
    }

    const auto program = metadataObject->getProperty(juce::Identifier("program"));
    if (!program.isVoid() && !program.isString())
        result.errors.push_back("metadata.program must be a string");

    validateBrowserMetadata(result, *metadataObject);
}

void validateModSlots(PresetValidationResult& result, const juce::var& modSlots)
{
    if (!modSlots.isArray())
        return;

    const auto* sourceSpec = findParameterSpec("transmod.1.source");
    if (sourceSpec == nullptr)
    {
        result.errors.push_back("transmod source parameter spec missing");
        return;
    }

    const auto* slots = modSlots.getArray();
    if (slots == nullptr)
        return;

    for (const auto& slotVar : *slots)
    {
        if (!slotVar.isObject())
        {
            result.errors.push_back("mod_slots entries must be objects");
            continue;
        }

        const auto* slot = slotVar.getDynamicObject();
        if (slot == nullptr)
        {
            result.errors.push_back("mod_slots entry object unavailable");
            continue;
        }

        const auto slotId = slot->getProperty(juce::Identifier("slot_id"));
        if (!slotId.isInt() || static_cast<int>(slotId) < 1 || static_cast<int>(slotId) > transModSlotCount)
            result.errors.push_back("mod_slots slot_id must be an integer from 1 through 8");

        const auto enabled = slot->getProperty(juce::Identifier("enabled"));
        const auto enabledValid = enabled.isBool();
        const auto isEnabled = enabledValid && static_cast<bool>(enabled);
        if (!enabledValid)
            result.errors.push_back("mod_slots enabled must be boolean");

        const auto source = slot->getProperty(juce::Identifier("source"));
        if (isEnabled && (!isAllowedChoice(*sourceSpec, source) || source.toString() == "None"))
            result.errors.push_back("mod_slots source must be a known non-None source");
        else if (!source.isVoid() && !isAllowedChoice(*sourceSpec, source))
            result.errors.push_back("mod_slots source must be None or a known source");

        const auto scaler = slot->getProperty(juce::Identifier("scaler"));
        if (!scaler.isVoid() && !isAllowedChoice(*sourceSpec, scaler))
            result.errors.push_back("mod_slots scaler must be None or a known source");

        const auto depthDomain = slot->getProperty(juce::Identifier("depth_domain"));
        auto depthDomainString = std::string {};
        if (!depthDomain.isString()
            || (depthDomain.toString() != "Physical" && depthDomain.toString() != "Normalized"))
        {
            result.errors.push_back("mod_slots depth_domain must be Physical or Normalized");
        }
        else
        {
            depthDomainString = depthDomain.toString().toStdString();
        }

        const auto depths = slot->getProperty(juce::Identifier("depths"));
        if (!depths.isObject())
        {
            result.errors.push_back("mod_slots depths must be an object");
            continue;
        }

        if (const auto* depthObject = depths.getDynamicObject())
        {
            for (const auto& property : depthObject->getProperties())
            {
                const auto targetId = property.name.toString().toStdString();
                if (!isAllowedModDestination(targetId))
                    result.errors.push_back("mod_slots unknown depth target: " + targetId);
                if (!isNumber(property.value))
                {
                    result.errors.push_back("mod_slots depth for " + targetId + " must be numeric");
                    continue;
                }

                const auto depth = static_cast<float>(static_cast<double>(property.value));
                if (!depthDomainString.empty() && !isDepthInRange(targetId, depthDomainString, depth))
                    result.errors.push_back("mod_slots depth for " + targetId + " is outside allowed range");
            }
        }
    }
}
} // namespace

PresetValidationResult validatePresetFile(const std::filesystem::path& path)
{
    PresetValidationResult result;
    result.path = path;

    if (!isPresetFilePath(path))
    {
        result.errors.push_back("preset extension must be .SynthiaPreset");
        return result;
    }

    const auto file = juceFileForPath(path);
    if (!file.existsAsFile())
    {
        result.errors.push_back("preset file missing");
        return result;
    }

    const auto parsed = juce::JSON::parse(file);
    if (!parsed.isObject())
    {
        result.errors.push_back("preset root must be a JSON object");
        return result;
    }

    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        result.errors.push_back("preset root object unavailable");
        return result;
    }

    const auto* object = presetPayloadObject(result, *root);
    if (object == nullptr)
        return result;

    const char* required[] = {
        "schema_version", "plugin_min_version", "id", "display_name",
        "author", "description", "tags", "parameters", "mod_slots", "macros"
    };

    for (const auto* field : required)
    {
        if (!hasProperty(*object, field))
            result.errors.push_back(std::string("missing required field: ") + field);
    }

    if (hasProperty(*object, "schema_version"))
    {
        const auto schema = getProperty(*object, "schema_version");
        if (!schema.isInt() || static_cast<int>(schema) < 1)
            result.errors.push_back("schema_version must be an integer >= 1");
    }

    if (hasProperty(*object, "tags"))
        validateTags(result, getProperty(*object, "tags"));

    if (hasProperty(*object, "mod_slots") && !getProperty(*object, "mod_slots").isArray())
        result.errors.push_back("mod_slots must be an array");
    else if (hasProperty(*object, "mod_slots"))
        validateModSlots(result, getProperty(*object, "mod_slots"));

    if (hasProperty(*object, "macros") && !getProperty(*object, "macros").isArray())
        result.errors.push_back("macros must be an array");

    if (hasProperty(*object, "parameters"))
    {
        const auto parameters = getProperty(*object, "parameters");
        if (!parameters.isObject())
        {
            result.errors.push_back("parameters must be an object");
        }
        else if (const auto* parameterObject = parameters.getDynamicObject())
        {
            for (const auto& property : parameterObject->getProperties())
            {
                const auto id = property.name.toString().toStdString();
                const auto* spec = findParameterSpec(id);

                if (spec == nullptr)
                {
                    result.errors.push_back("unknown parameter id: " + id);
                    continue;
                }

                validateParameterValue(result, *spec, property.value);
            }
        }
    }

    if (hasProperty(*object, "metadata"))
        validateMetadata(result, getProperty(*object, "metadata"));

    return result;
}

std::vector<PresetValidationResult> validatePresetDirectory(const std::filesystem::path& directory)
{
    std::vector<PresetValidationResult> results;

    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error)
    {
        PresetValidationResult result;
        result.path = directory;
        result.errors.push_back("preset directory missing");
        results.push_back(result);
        return results;
    }

    std::filesystem::recursive_directory_iterator iterator { directory,
                                                             std::filesystem::directory_options::skip_permission_denied,
                                                             error };
    if (error)
    {
        PresetValidationResult result;
        result.path = directory;
        result.errors.push_back("preset directory unavailable: " + error.message());
        results.push_back(result);
        return results;
    }

    for (std::filesystem::recursive_directory_iterator end; iterator != end; iterator.increment(error))
    {
        if (error)
        {
            error.clear();
            continue;
        }

        const auto& entry = *iterator;
        if (entry.is_regular_file(error) && !error && isPresetFilePath(entry.path()))
            results.push_back(validatePresetFile(entry.path()));

        error.clear();
    }

    if (results.empty())
    {
        PresetValidationResult result;
        result.path = directory;
        result.errors.push_back("preset directory contains no Synthia preset files");
        results.push_back(result);
    }

    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return a.path.string() < b.path.string();
    });

    return results;
}
} // namespace synth
