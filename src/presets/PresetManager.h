#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace synth
{
enum class PresetSource
{
    Factory,
    User,
};

enum class PresetWriteMode
{
    CreateNew,
    OverwriteExisting,
};

struct PresetSummary
{
    std::filesystem::path path;
    std::string id;
    std::string displayName;
    std::string author;
    std::string description;
    std::string bank;
    std::string category;
    std::vector<std::string> tags;
    PresetSource source = PresetSource::User;
    bool factory = false;
    bool favorite = false;
    std::string favoriteKey;
};

struct PresetBrowserFilter
{
    std::string searchText;
    std::string bank;
    std::string category;
    std::vector<std::string> tags;
    bool favoritesOnly = false;
    bool includeFactory = true;
    bool includeUser = true;
};

struct PresetBrowserCatalog
{
    std::vector<PresetSummary> presets;
    std::vector<std::string> banks;
    std::vector<std::string> categories;
    std::vector<std::string> tags;
};

struct PresetStateFingerprint
{
    bool valid = false;
    int comparedParameterCount = 0;
    std::uint64_t hash = 0;
};

struct PresetLoadResult
{
    bool loaded = false;
    std::string message;
    std::string displayName;
    juce::ValueTree state;
    PresetStateFingerprint fingerprint;
};

struct PresetWriteMetadata
{
    std::string displayName;
    std::string author = "User";
    std::string description = "User preset saved from the Synthia editor.";
    std::vector<std::string> tags { "user" };
    std::string bank = "User";
    std::string category = "User";
};

struct PresetWriteOptions
{
    PresetWriteMetadata metadata;
    PresetWriteMode mode = PresetWriteMode::OverwriteExisting;
};

struct PresetDirtyState
{
    PresetStateFingerprint current;
    PresetStateFingerprint baseline;
    bool dirty = false;
};

struct PresetCompareSlot
{
    bool captured = false;
    std::string label;
    juce::ValueTree state;
    PresetStateFingerprint fingerprint;
};

std::vector<PresetSummary> scanPresetDirectory(const std::filesystem::path& directory, bool factory);
std::vector<PresetSummary> scanPresetDirectory(const std::filesystem::path& directory,
                                               PresetSource source,
                                               const std::vector<std::string>& favoriteKeys);
std::vector<PresetSummary> filterPresetSummaries(const std::vector<PresetSummary>& presets,
                                                 const PresetBrowserFilter& filter);
PresetBrowserCatalog buildPresetBrowserCatalog(const std::vector<PresetSummary>& presets);
std::filesystem::path factoryPresetDirectory();
std::filesystem::path defaultUserPresetDirectory();
std::filesystem::path defaultPresetFavoritesFile();
std::string presetIdFromDisplayName(const std::string& displayName);
std::string presetSourceId(PresetSource source);
std::string presetSourceLabel(PresetSource source);
std::string presetFavoriteKey(PresetSource source, const std::string& presetId, const std::filesystem::path& path);
std::vector<std::string> readFavoritePresetKeys(const std::filesystem::path& path);
bool writeFavoritePresetKeys(const std::filesystem::path& path,
                             const std::vector<std::string>& favoriteKeys,
                             std::string& error);
bool setPresetFavorite(const std::filesystem::path& path,
                       const std::string& favoriteKey,
                       bool favorite,
                       std::string& error);

PresetLoadResult preparePresetState(juce::AudioProcessorValueTreeState& parameters,
                                    const std::filesystem::path& path);
PresetLoadResult prepareInitPresetState(juce::AudioProcessorValueTreeState& parameters);
PresetLoadResult prepareRandomizedPresetState(juce::AudioProcessorValueTreeState& parameters,
                                              std::uint32_t seed);
PresetStateFingerprint fingerprintCurrentPresetState(const juce::AudioProcessorValueTreeState& parameters);
PresetStateFingerprint fingerprintPresetState(const juce::ValueTree& state);
PresetDirtyState comparePresetDirtyState(const juce::AudioProcessorValueTreeState& parameters,
                                         const PresetStateFingerprint& baseline);
PresetCompareSlot capturePresetCompareSlot(juce::AudioProcessorValueTreeState& parameters,
                                           const std::string& label);
PresetLoadResult preparePresetCompareSlotState(juce::AudioProcessorValueTreeState& parameters,
                                               const PresetCompareSlot& slot);

std::optional<float> parseStateParameterNumber(const juce::var& value);
bool writeOwnedStateFile(const std::filesystem::path& path, const juce::String& text, bool overwrite, std::string& error);

juce::ValueTree mergeParameterStateWithDefaults(juce::AudioProcessorValueTreeState& parameters,
                                                const juce::ValueTree& overrideState);

PresetLoadResult loadPresetIntoState(juce::AudioProcessorValueTreeState& parameters,
                                     const std::filesystem::path& path);

bool writeCurrentPreset(const juce::AudioProcessorValueTreeState& parameters,
                        const std::filesystem::path& path,
                        const std::string& displayName,
                        std::string& error);
bool writeCurrentPreset(const juce::AudioProcessorValueTreeState& parameters,
                        const std::filesystem::path& path,
                        const PresetWriteOptions& options,
                        std::string& error);
} // namespace synth
