#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace synth
{
struct PresetValidationResult
{
    std::filesystem::path path;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool passed() const noexcept { return errors.empty(); }
};

PresetValidationResult validatePresetFile(const std::filesystem::path& path);
std::vector<PresetValidationResult> validatePresetDirectory(const std::filesystem::path& directory);
} // namespace synth
