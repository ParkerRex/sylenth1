#include "../dsp/Filter.h"
#include "../dsp/OscillatorStack.h"
#include "../dsp/SynthEngine.h"
#include "../modulation/ModulationRouteModel.h"
#include "../plugin/ParameterRegistry.h"
#include "../presets/PresetManager.h"
#include "../presets/PresetValidator.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{
struct Options
{
    bool smoke = false;
    bool listParameters = false;
    bool validatePresets = false;
    bool voiceTest = false;
    bool oscTest = false;
    bool filterTest = false;
    bool modulationTest = false;
    bool modulationRouteRenderTest = false;
    bool offlineRealtimeCompareTest = false;
    bool randomizeTest = false;
    bool presetRender = false;
    bool dry = false;
    bool wet = false;
    bool bench = false;
    std::string benchPreset = "supersaw-stack-01";
    std::string benchNotes = "A2,E3,A3,C4,E4,A4";
    double benchSeconds = 8.0;
    int benchReps = 5;
    int benchBlock = 512;
    std::string suite;
    std::filesystem::path output = "build/reports/smoke.json";
    std::filesystem::path report = "build/reports/render.json";
    std::filesystem::path outputDir = "build/reports/core";
    std::filesystem::path presetPath = "presets/factory";
    std::filesystem::path fixturePath = "fixtures/midi/overlap-pluck.mid";
    std::string notes = "C1,C3,C5,C7";
    std::vector<std::uint32_t> randomizeSeeds { 1u, 42u, 12345u, 67890u };
    std::string argumentError;
};

struct RenderEvent
{
    int sample = 0;
    juce::MidiMessage message;
};

struct SuiteItem
{
    std::string name;
    std::filesystem::path reportPath;
    bool passed = false;
};

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

bool parseSeedList(const std::string& seedList,
                   std::vector<std::uint32_t>& seeds,
                   std::string& error)
{
    std::vector<std::uint32_t> parsedSeeds;
    auto start = std::size_t { 0 };

    if (seedList.empty())
    {
        error = "--seeds requires at least one unsigned 32-bit integer";
        return false;
    }

    if (seedList.back() == ',')
    {
        error = "--seeds contains an empty seed";
        return false;
    }

    while (start < seedList.size())
    {
        const auto comma = seedList.find(',', start);
        auto token = seedList.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        auto first = std::size_t { 0 };
        while (first < token.size() && std::isspace(static_cast<unsigned char>(token[first])) != 0)
            ++first;

        auto last = token.size();
        while (last > first && std::isspace(static_cast<unsigned char>(token[last - 1])) != 0)
            --last;

        token = token.substr(first, last - first);

        if (token.empty())
        {
            error = "--seeds contains an empty seed";
            return false;
        }

        try
        {
            auto consumed = std::size_t { 0 };
            const auto value = std::stoull(token, &consumed, 10);
            if (consumed != token.size())
            {
                error = "invalid --seeds token: " + token;
                return false;
            }

            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                error = "out-of-range --seeds token: " + token;
                return false;
            }

            parsedSeeds.push_back(static_cast<std::uint32_t>(value));
        }
        catch (const std::exception&)
        {
            error = "invalid --seeds token: " + token;
            return false;
        }

        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    if (parsedSeeds.empty())
    {
        error = "--seeds requires at least one unsigned 32-bit integer";
        return false;
    }

    seeds = std::move(parsedSeeds);
    return true;
}

Options parseOptions(int argc, char* argv[])
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];

        if (arg == "--smoke")
        {
            options.smoke = true;
        }
        else if (arg == "--list-parameters")
        {
            options.listParameters = true;
            options.output = "build/reports/parameters.json";
        }
        else if (arg == "--validate-presets" && i + 1 < argc)
        {
            options.validatePresets = true;
            options.presetPath = argv[++i];
            options.output = "build/reports/presets.json";
        }
        else if (arg == "--voice-test")
        {
            options.voiceTest = true;
            options.output = "build/reports/voice-core.json";
        }
        else if (arg == "--osc-test")
        {
            options.oscTest = true;
            options.output = "build/reports/oscillator.json";
        }
        else if (arg == "--filter-test")
        {
            options.filterTest = true;
            options.output = "build/reports/filter.json";
        }
        else if (arg == "--modulation-test")
        {
            options.modulationTest = true;
            options.output = "build/reports/modulation.json";
        }
        else if (arg == "--modulation-route-render-test")
        {
            options.modulationRouteRenderTest = true;
            options.output = "build/reports/modulation-route-render.json";
        }
        else if (arg == "--offline-realtime-compare-test")
        {
            options.offlineRealtimeCompareTest = true;
            options.presetPath = "presets/factory/FX/FX - Space 01.SynthiaPreset";
            options.output = "build/reports/offline-realtime-compare.json";
        }
        else if (arg == "--randomize-test")
        {
            options.randomizeTest = true;
            options.output = "build/reports/randomize.json";
        }
        else if (arg == "--seeds")
        {
            if (i + 1 >= argc)
            {
                options.argumentError = "--seeds requires a comma-separated value";
                continue;
            }

            auto error = std::string {};
            if (!parseSeedList(argv[++i], options.randomizeSeeds, error) && options.argumentError.empty())
                options.argumentError = error;
        }
        else if (arg == "--suite" && i + 1 < argc)
        {
            options.suite = argv[++i];
        }
        else if (arg == "--output-dir" && i + 1 < argc)
        {
            options.outputDir = argv[++i];
        }
        else if (arg == "--preset" && i + 1 < argc)
        {
            options.presetRender = true;
            options.presetPath = argv[++i];
        }
        else if (arg == "--fixture" && i + 1 < argc)
        {
            options.fixturePath = argv[++i];
        }
        else if (arg == "--dry")
        {
            options.dry = true;
        }
        else if (arg == "--wet")
        {
            options.wet = true;
        }
        else if (arg == "--notes" && i + 1 < argc)
        {
            options.notes = argv[++i];
        }
        else if (arg == "--bench")
        {
            options.bench = true;
            options.output = "build/reports/bench.json";
        }
        else if (arg == "--bench-preset" && i + 1 < argc)
        {
            options.benchPreset = argv[++i];
        }
        else if (arg == "--bench-notes" && i + 1 < argc)
        {
            options.benchNotes = argv[++i];
        }
        else if (arg == "--bench-seconds" && i + 1 < argc)
        {
            options.benchSeconds = std::strtod(argv[++i], nullptr);
        }
        else if (arg == "--bench-reps" && i + 1 < argc)
        {
            options.benchReps = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (arg == "--bench-block" && i + 1 < argc)
        {
            options.benchBlock = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            options.output = argv[++i];
        }
        else if (arg == "--report" && i + 1 < argc)
        {
            options.report = argv[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage:\n";
            std::cout << "  SynthiaRender --smoke --output <path>\n";
            std::cout << "  SynthiaRender --list-parameters --output <path>\n";
            std::cout << "  SynthiaRender --validate-presets <dir> --output <path>\n";
            std::cout << "  SynthiaRender --voice-test --output <path>\n";
            std::cout << "  SynthiaRender --osc-test --notes C1,C3,C5,C7 --output <path>\n";
            std::cout << "  SynthiaRender --filter-test --output <path>\n";
            std::cout << "  SynthiaRender --modulation-test --fixture <path> --output <path>\n";
            std::cout << "  SynthiaRender --modulation-route-render-test --fixture <path> --output <path>\n";
            std::cout << "  SynthiaRender --offline-realtime-compare-test --preset <json> --fixture <path> --output <path>\n";
            std::cout << "  SynthiaRender --randomize-test --seeds 1,42,12345 --fixture <path> --output <path>\n";
            std::cout << "  SynthiaRender --suite core --output-dir <dir>\n";
            std::cout << "  SynthiaRender --suite patch-recreation --output-dir <dir>\n";
            std::cout << "  SynthiaRender --preset <json> --fixture <path> --dry --output <wav> --report <json>\n";
            std::cout << "  SynthiaRender --preset <json> --fixture <path> --wet --output <wav> --report <json>\n";
            std::exit(0);
        }
    }

    return options;
}

void ensureParentDirectory(const std::filesystem::path& path)
{
    if (path.parent_path() != std::filesystem::path {})
        std::filesystem::create_directories(path.parent_path());
}

void ensureDirectory(const std::filesystem::path& path)
{
    if (path != std::filesystem::path {})
        std::filesystem::create_directories(path);
}

const char* boolString(bool value) noexcept
{
    return value ? "true" : "false";
}

std::string genericString(const std::filesystem::path& path)
{
    return path.generic_string();
}

std::string jsonString(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');

    for (const auto c : value)
    {
        switch (c)
        {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(c); break;
        }
    }

    escaped.push_back('"');
    return escaped;
}

std::string jsonString(const std::filesystem::path& path)
{
    return jsonString(genericString(path));
}

void writeSmokeReport(const std::filesystem::path& path, const synth::RenderStats& stats)
{
    ensureParentDirectory(path);

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"smoke\",\n";
    out << "  \"samples_rendered\": " << stats.samplesRendered << ",\n";
    out << "  \"invalid_samples\": " << stats.invalidSamples << ",\n";
    out << "  \"active_voices\": " << stats.activeVoices << ",\n";
    out << "  \"peak\": " << stats.peak << ",\n";
    out << "  \"passed\": " << (stats.invalidSamples == 0 ? "true" : "false") << "\n";
    out << "}\n";
}

void writeParameterReport(const std::filesystem::path& path)
{
    ensureParentDirectory(path);

    const auto errors = synth::validateParameterSpecs();
    const auto& specs = synth::getParameterSpecs();

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"parameter_count\": " << specs.size() << ",\n";
    out << "  \"error_count\": " << errors.size() << ",\n";
    out << "  \"passed\": " << (errors.empty() ? "true" : "false") << ",\n";
    out << "  \"parameters\": [\n";

    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const auto& spec = specs[i];
        out << "    {";
        out << "\"id\": \"" << spec.id << "\", ";
        out << "\"name\": \"" << spec.name << "\", ";
        out << "\"group\": \"" << spec.group << "\", ";
        out << "\"kind\": \"" << synth::toString(spec.kind) << "\", ";
        out << "\"unit\": \"" << spec.unit << "\", ";
        out << "\"default\": " << spec.defaultValue;
        out << "}";
        out << (i + 1 == specs.size() ? "\n" : ",\n");
    }

    out << "  ],\n";
    out << "  \"errors\": [\n";
    for (std::size_t i = 0; i < errors.size(); ++i)
        out << "    \"" << errors[i] << "\"" << (i + 1 == errors.size() ? "\n" : ",\n");
    out << "  ]\n";
    out << "}\n";
}

int writePresetReport(const std::filesystem::path& path, const std::filesystem::path& presetPath)
{
    ensureParentDirectory(path);

    const auto results = synth::validatePresetDirectory(presetPath);
    int errorCount = 0;
    int warningCount = 0;

    for (const auto& result : results)
    {
        errorCount += static_cast<int>(result.errors.size());
        warningCount += static_cast<int>(result.warnings.size());
    }

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"preset_path\": \"" << presetPath.string() << "\",\n";
    out << "  \"preset_count\": " << results.size() << ",\n";
    out << "  \"error_count\": " << errorCount << ",\n";
    out << "  \"warning_count\": " << warningCount << ",\n";
    out << "  \"passed\": " << (errorCount == 0 ? "true" : "false") << ",\n";
    out << "  \"presets\": [\n";

    for (std::size_t i = 0; i < results.size(); ++i)
    {
        const auto& result = results[i];
        out << "    {";
        out << "\"path\": \"" << result.path.string() << "\", ";
        out << "\"passed\": " << (result.passed() ? "true" : "false") << ", ";
        out << "\"error_count\": " << result.errors.size() << ", ";
        out << "\"warning_count\": " << result.warnings.size();
        if (!result.errors.empty())
        {
            out << ", \"errors\": [";
            for (std::size_t errorIndex = 0; errorIndex < result.errors.size(); ++errorIndex)
                out << (errorIndex > 0 ? ", " : "") << jsonString(result.errors[errorIndex]);
            out << "]";
        }
        out << "}";
        out << (i + 1 == results.size() ? "\n" : ",\n");
    }

    out << "  ]\n";
    out << "}\n";

    return errorCount == 0 ? 0 : 1;
}

int writeVoiceReport(const std::filesystem::path& path)
{
    ensureParentDirectory(path);

    synth::SynthEngine engine;
    engine.prepare(48000.0, 128);
    engine.noteOn(60, 1.0f);

    std::vector<float> left(128);
    std::vector<float> right(128);
    auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    const auto activeAfterOn = stats.activeVoices;

    engine.noteOff(60);
    for (int i = 0; i < 200; ++i)
        stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));

    const auto activeAfterRelease = stats.activeVoices;
    const auto passed = activeAfterOn == 1 && activeAfterRelease == 0 && stats.invalidSamples == 0;

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"voice-core\",\n";
    out << "  \"active_after_note_on\": " << activeAfterOn << ",\n";
    out << "  \"active_after_release\": " << activeAfterRelease << ",\n";
    out << "  \"peak_after_note_on\": " << stats.peak << ",\n";
    out << "  \"invalid_samples\": " << stats.invalidSamples << ",\n";
    out << "  \"passed\": " << (passed ? "true" : "false") << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

float estimateFrequency(const std::vector<float>& samples, double sampleRate)
{
    int crossings = 0;
    auto first = -1;
    auto last = -1;

    for (int i = 1; i < static_cast<int>(samples.size()); ++i)
    {
        if (samples[static_cast<std::size_t>(i - 1)] <= 0.0f && samples[static_cast<std::size_t>(i)] > 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }

    if (crossings < 2 || first < 0 || last <= first)
        return 0.0f;

    return static_cast<float>((static_cast<double>(crossings - 1) * sampleRate) / static_cast<double>(last - first));
}

float centsBetween(float actual, float expected)
{
    if (actual <= 0.0f || expected <= 0.0f)
        return 9999.0f;

    return 1200.0f * std::log2(actual / expected);
}

double highBandEnergyRatio(const std::vector<float>& samples)
{
    constexpr auto maxBins = 512;
    double total = 0.0;
    double high = 0.0;
    const auto sampleCount = static_cast<int>(samples.size());

    for (int bin = 1; bin <= maxBins; ++bin)
    {
        double real = 0.0;
        double imag = 0.0;
        const auto radiansPerSample = -2.0 * juce::MathConstants<double>::pi * static_cast<double>(bin)
            / static_cast<double>(sampleCount);
        for (int i = 0; i < sampleCount; ++i)
        {
            const auto angle = radiansPerSample * static_cast<double>(i);
            real += static_cast<double>(samples[static_cast<std::size_t>(i)]) * std::cos(angle);
            imag += static_cast<double>(samples[static_cast<std::size_t>(i)]) * std::sin(angle);
        }

        const auto energy = real * real + imag * imag;
        total += energy;
        if (bin > static_cast<int>(maxBins * 0.85))
            high += energy;
    }

    return total > 0.0 ? high / total : 0.0;
}

int noteNameToMidi(std::string note)
{
    if (note.empty())
        return 60;

    note.erase(std::remove_if(note.begin(), note.end(), [](unsigned char c) {
                   return std::isspace(c) != 0;
               }),
               note.end());

    const auto letter = static_cast<char>(std::toupper(static_cast<unsigned char>(note[0])));
    auto semitone = 0;
    switch (letter)
    {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: return 60;
    }

    auto index = std::size_t { 1 };
    if (index < note.size() && (note[index] == '#' || note[index] == 'b'))
    {
        semitone += note[index] == '#' ? 1 : -1;
        ++index;
    }

    const auto octave = index < note.size() ? std::stoi(note.substr(index)) : 4;
    return (octave + 1) * 12 + semitone;
}

std::vector<int> parseNotes(const std::string& notes)
{
    std::vector<int> result;
    auto start = std::size_t { 0 };

    while (start < notes.size())
    {
        const auto comma = notes.find(',', start);
        const auto token = notes.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        result.push_back(noteNameToMidi(token));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }

    return result.empty() ? std::vector<int> { 60 } : result;
}

int writeOscillatorReport(const std::filesystem::path& path, const std::string& noteList)
{
    ensureParentDirectory(path);

    constexpr auto sampleRate = 48000.0;
    constexpr auto sampleCount = 48000;
    auto passed = true;
    const auto notes = parseNotes(noteList);

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"oscillator\",\n";
    out << "  \"sample_rate\": " << sampleRate << ",\n";
    out << "  \"alias_high_band_ratios\": [\n";
    for (std::size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex)
    {
        synth::OscillatorStack oscillator;
        oscillator.prepare(sampleRate);
        oscillator.reset(0.0f);

        synth::SynthParameters parameters;
        parameters.osc.sawLevel = 1.0f;
        parameters.osc.pulseLevel = 0.0f;
        parameters.osc.subLevel = 0.0f;
        parameters.osc.noiseLevel = 0.0f;
        parameters.amp.analog = 0.0f;

        std::vector<float> aliasSamples(4096);
        for (auto& sample : aliasSamples)
            sample = oscillator.renderSample(static_cast<float>(notes[noteIndex]), parameters, 0.0f, 0.0f);

        out << "    {\"midi\": " << notes[noteIndex]
            << ", \"high_band_ratio\": " << highBandEnergyRatio(aliasSamples) << "}";
        out << (noteIndex + 1 == notes.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"notes\": [\n";

    for (std::size_t noteIndex = 0; noteIndex < notes.size(); ++noteIndex)
    {
        synth::OscillatorStack oscillator;
        oscillator.prepare(sampleRate);
        oscillator.reset(0.0f);

        synth::SynthParameters parameters;
        parameters.osc.sawLevel = 1.0f;
        parameters.osc.pulseLevel = 0.0f;
        parameters.osc.subLevel = 0.0f;
        parameters.osc.noiseLevel = 0.0f;
        parameters.amp.analog = 0.0f;

        std::vector<float> samples(sampleCount);
        for (auto& sample : samples)
            sample = oscillator.renderSample(static_cast<float>(notes[noteIndex]), parameters, 0.0f, 0.0f);

        const auto frequency = estimateFrequency(samples, sampleRate);
        const auto expected = synth::midiNoteToHz(static_cast<float>(notes[noteIndex]));
        const auto cents = centsBetween(frequency, expected);
        const auto notePassed = std::abs(cents) < 5.0f;
        passed = passed && notePassed;

        out << "    {";
        out << "\"midi\": " << notes[noteIndex] << ", ";
        out << "\"expected_hz\": " << expected << ", ";
        out << "\"measured_hz\": " << frequency << ", ";
        out << "\"cents_error\": " << cents << ", ";
        out << "\"passed\": " << (notePassed ? "true" : "false");
        out << "}";
        out << (noteIndex + 1 == notes.size() ? "\n" : ",\n");
    }

    out << "  ],\n";
    out << "  \"pulse_widths\": [\n";
    const float pulseWidths[] = { 0.10f, 0.25f, 0.50f, 0.75f, 0.90f };
    auto pulsePassed = true;
    for (int widthIndex = 0; widthIndex < 5; ++widthIndex)
    {
        synth::OscillatorStack pulse;
        pulse.prepare(sampleRate);
        pulse.reset(0.0f);
        synth::SynthParameters pulseParameters;
        pulseParameters.osc.sawLevel = 0.0f;
        pulseParameters.osc.pulseLevel = 1.0f;
        pulseParameters.osc.pulseWidth = pulseWidths[widthIndex];
        int positive = 0;
        for (int i = 0; i < sampleCount; ++i)
        {
            if (pulse.renderSample(48, pulseParameters, 0.0f, 0.0f) > 0.0f)
                ++positive;
        }

        const auto duty = static_cast<float>(positive) / static_cast<float>(sampleCount);
        const auto widthPassed = std::abs(duty - pulseWidths[widthIndex]) < 0.04f;
        pulsePassed = pulsePassed && widthPassed;
        out << "    {\"target\": " << pulseWidths[widthIndex]
            << ", \"positive_ratio\": " << duty
            << ", \"passed\": " << (widthPassed ? "true" : "false") << "}";
        out << (widthIndex == 4 ? "\n" : ",\n");
    }
    passed = passed && pulsePassed;
    out << "  ],\n";
    out << "  \"sub_octaves\": [\n";
    auto subPassed = true;
    for (int octave = 1; octave <= 3; ++octave)
    {
        synth::OscillatorStack oscillator;
        oscillator.prepare(sampleRate);
        oscillator.reset(0.0f);
        synth::SynthParameters parameters;
        parameters.osc.sawLevel = 0.0f;
        parameters.osc.pulseLevel = 0.0f;
        parameters.osc.subLevel = 1.0f;
        parameters.osc.subWave = synth::SubWave::Saw;
        parameters.osc.subOctave = octave;
        std::vector<float> samples(sampleCount);
        for (auto& sample : samples)
            sample = oscillator.renderSample(60, parameters, 0.0f, 0.0f);

        const auto frequency = estimateFrequency(samples, sampleRate);
        const auto expected = synth::midiNoteToHz(static_cast<float>(60 - octave * 12));
        const auto cents = centsBetween(frequency, expected);
        const auto octavePassed = std::abs(cents) < 5.0f;
        subPassed = subPassed && octavePassed;
        out << "    {\"octave\": -" << octave
            << ", \"measured_hz\": " << frequency
            << ", \"cents_error\": " << cents
            << ", \"passed\": " << (octavePassed ? "true" : "false") << "}";
        out << (octave == 3 ? "\n" : ",\n");
    }
    passed = passed && subPassed;
    out << "  ],\n";

    auto detunePassed = true;
    for (int count = 1; count <= 5; ++count)
    {
        for (int index = 0; index < count; ++index)
        {
            const auto opposite = count - 1 - index;
            const auto sum = synth::OscillatorStack::detuneOffsetCents(index, count, 0.7f)
                + synth::OscillatorStack::detuneOffsetCents(opposite, count, 0.7f);
            detunePassed = detunePassed && std::abs(sum) < 0.0001f;
        }
    }

    synth::OscillatorStack noiseA;
    synth::OscillatorStack noiseB;
    noiseA.prepare(sampleRate);
    noiseB.prepare(sampleRate);
    noiseA.reset(0.25f);
    noiseB.reset(0.25f);
    synth::SynthParameters noiseParameters;
    noiseParameters.osc.sawLevel = 0.0f;
    noiseParameters.osc.pulseLevel = 0.0f;
    noiseParameters.osc.subLevel = 0.0f;
    noiseParameters.osc.noiseLevel = 1.0f;
    auto noisePassed = true;
    for (int i = 0; i < 512; ++i)
        noisePassed = noisePassed && std::abs(noiseA.renderSample(60, noiseParameters, 0.0f, 0.0f) - noiseB.renderSample(60, noiseParameters, 0.0f, 0.0f)) < 1.0e-7f;

    synth::OscillatorStack sync;
    sync.prepare(sampleRate);
    sync.reset(0.0f);
    synth::SynthParameters syncParameters;
    syncParameters.osc.syncAmount = 1.0f;
    auto syncPassed = true;
    for (int i = 0; i < 4096; ++i)
    {
        const auto sample = sync.renderSample(96, syncParameters, 0.0f, 0.0f);
        syncPassed = syncPassed && std::isfinite(sample) && std::abs(sample) <= 1.21f;
    }

    passed = passed && detunePassed && noisePassed && syncPassed;
    out << "  \"stack_detune_symmetry_passed\": " << (detunePassed ? "true" : "false") << ",\n";
    out << "  \"noise_determinism_passed\": " << (noisePassed ? "true" : "false") << ",\n";
    out << "  \"hard_sync_stability_passed\": " << (syncPassed ? "true" : "false") << ",\n";
    out << "  \"pulse_width_passed\": " << (pulsePassed ? "true" : "false") << ",\n";
    out << "  \"sub_octaves_passed\": " << (subPassed ? "true" : "false") << ",\n";
    out << "  \"passed\": " << (passed ? "true" : "false") << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

float renderFilterEnergy(synth::FilterMode mode, float drive, float resonance, int oversampling)
{
    synth::Filter filter;
    filter.prepare(48000.0);

    synth::SynthParameters parameters;
    parameters.filter.mode = mode;
    parameters.filter.cutoffSemitones = 72.0f;
    parameters.filter.resonance = resonance;
    parameters.filter.drive = drive;
    parameters.filter.oversampling = oversampling;

    auto energy = 0.0f;
    for (int i = 0; i < 24000; ++i)
    {
        const auto input = i == 0 ? 1.0f : 0.0f;
        const auto output = filter.process(input, 60, parameters, 0.0f);
        if (!std::isfinite(output))
            return -1.0f;
        energy += output * output;
    }

    return energy;
}

int writeFilterReport(const std::filesystem::path& path)
{
    ensureParentDirectory(path);

    const auto cutoffHz = synth::Filter::cutoffSemitonesToHz(69.0f);
    const auto mappingPassed = std::abs(cutoffHz - 440.0f) < 0.1f;
    auto stabilityPassed = true;

    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"filter\",\n";
    out << "  \"cutoff_69_semitones_hz\": " << cutoffHz << ",\n";
    out << "  \"cutoff_mapping_passed\": " << (mappingPassed ? "true" : "false") << ",\n";
    out << "  \"mode_energies\": [\n";

    const synth::FilterMode modes[] = {
        synth::FilterMode::L2, synth::FilterMode::L4, synth::FilterMode::B2,
        synth::FilterMode::B4, synth::FilterMode::H2, synth::FilterMode::H4,
        synth::FilterMode::Peak2, synth::FilterMode::Notch2, synth::FilterMode::Notch4
    };

    for (std::size_t i = 0; i < 9; ++i)
    {
        const auto energy = renderFilterEnergy(modes[i], 0.35f, 0.45f, 1);
        stabilityPassed = stabilityPassed && energy >= 0.0f && energy < 20000.0f;
        out << "    {\"mode\": " << i << ", \"energy\": " << energy << "}";
        out << (i + 1 == 9 ? "\n" : ",\n");
    }

    out << "  ],\n";

    const auto lowDrive = renderFilterEnergy(synth::FilterMode::L4, 0.0f, 0.75f, 1);
    const auto highDrive = renderFilterEnergy(synth::FilterMode::L4, 0.85f, 0.75f, 2);
    const auto drivePassed = lowDrive >= 0.0f && highDrive >= 0.0f && std::abs(highDrive - lowDrive) > 0.00001f;
    const auto passed = mappingPassed && stabilityPassed && drivePassed;

    out << "  \"low_drive_energy\": " << lowDrive << ",\n";
    out << "  \"high_drive_energy\": " << highDrive << ",\n";
    out << "  \"drive_changes_response\": " << (drivePassed ? "true" : "false") << ",\n";
    out << "  \"stability_passed\": " << (stabilityPassed ? "true" : "false") << ",\n";
    out << "  \"passed\": " << (passed ? "true" : "false") << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

int choiceIndex(const synth::ParameterSpec& spec, const juce::var& value)
{
    if (value.isString())
    {
        const auto selected = value.toString().toStdString();
        const auto found = std::find(spec.choices.begin(), spec.choices.end(), selected);
        if (found != spec.choices.end())
            return static_cast<int>(std::distance(spec.choices.begin(), found));
    }

    if (value.isInt() || value.isInt64() || value.isDouble())
        return static_cast<int>(std::round(static_cast<double>(value)));

    return spec.defaultChoice;
}

float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

void applyPresetValue(synth::SynthParameters& parameters, const std::string& id, const juce::var& value)
{
    const auto* spec = synth::findParameterSpec(id);
    if (spec == nullptr)
        return;

    const auto numeric = value.isBool() ? (static_cast<bool>(value) ? 1.0f : 0.0f)
                                        : (value.isInt() || value.isInt64() || value.isDouble()
                                               ? finiteOr(static_cast<float>(static_cast<double>(value)), spec->defaultValue)
                                               : spec->defaultValue);
    const auto choice = spec->kind == synth::ParameterKind::Choice ? choiceIndex(*spec, value) : 0;

    if (id.starts_with("lfo.2.") || id.starts_with("mod_env.2."))
    {
        synth::SynthParameters translated;
        const auto isLfo = id.starts_with("lfo.2.");
        if (isLfo)
        {
            // Preserve loaded LFO fields while sharing the first LFO field mapping.
            translated.lfo = parameters.lfo2;
            applyPresetValue(translated, "lfo." + id.substr(6), value);
            parameters.lfo2 = translated.lfo;
        }
        else
        {
            translated.modEnv = parameters.modEnv2;
            applyPresetValue(translated, "mod_env." + id.substr(10), value);
            parameters.modEnv2 = translated.modEnv;
        }
        return;
    }

    if (id == "global.sync") parameters.sync = parameters.arp.sync = numeric >= 0.5f;
    else if (id == "voice.pitch_bend_range") parameters.pitchBendRange = numeric;
    else if (id == "voice.portamento_mode") parameters.portamentoMode = static_cast<synth::PortamentoMode>(choice);
    else if (id == "master.level_db") parameters.master.levelDb = numeric;
    else if (id == "filter_control.cutoff_semitones") parameters.filterControl.cutoffSemitones = numeric;
    else if (id == "filter_control.resonance") parameters.filterControl.resonance = numeric;
    else if (id == "filter_control.keytrack") parameters.filterControl.keytrack = numeric;
    else if (id == "filter_control.drive") parameters.filterControl.drive = numeric;
    else if (id == "filter_control.warm_drive") parameters.filterControl.warmDrive = numeric >= 0.5f;
    else if (id == "voice.mode") parameters.voiceMode = static_cast<synth::VoiceMode>(choice);
    else if (id == "voice.polyphony") parameters.polyphony = static_cast<int>(std::round(numeric));
    else if (id == "voice.unison_count") parameters.unisonCount = static_cast<int>(std::round(numeric));
    else if (id == "voice.retrigger") parameters.retrigger = numeric >= 0.5f;
    else if (id == "voice.glide_ms") parameters.glideMs = numeric;
    else if (id == "voice.velocity_glide_ms") parameters.velocityGlideMs = numeric;
    else if (id == "mod_env.attack_ms") parameters.modEnv.attackMs = numeric;
    else if (id == "mod_env.decay_ms") parameters.modEnv.decayMs = numeric;
    else if (id == "mod_env.sustain") parameters.modEnv.sustain = numeric;
    else if (id == "mod_env.release_ms") parameters.modEnv.releaseMs = numeric;
    else if (id == "lfo.shape") parameters.lfo.shape = static_cast<synth::LfoShapeChoice>(choice);
    else if (id == "lfo.rate_mode") parameters.lfo.rateMode = static_cast<synth::LfoRateMode>(choice);
    else if (id == "lfo.rate_hz") parameters.lfo.rateHz = numeric;
    else if (id == "lfo.sync_division") parameters.lfo.syncDivision = choice;
    else if (id == "lfo.phase_degrees") parameters.lfo.phaseDegrees = numeric;
    else if (id == "lfo.gate_mode") parameters.lfo.gateMode = static_cast<synth::LfoGateMode>(choice);
    else if (id == "lfo.mono") parameters.lfo.mono = numeric >= 0.5f;
    else if (id == "lfo.swing") parameters.lfo.swing = numeric;
    else if (id == "lfo.gain") parameters.lfo.gain = numeric;
    else if (id == "lfo.offset") parameters.lfo.offset = numeric;
    else if (id == "lfo.free") parameters.lfo.free = numeric >= 0.5f;
    else if (id == "lfo.step_count") parameters.lfo.stepCount = static_cast<int>(std::round(numeric));
    else if (id == "lfo.step_smooth") parameters.lfo.stepSmooth = numeric;
    else if (id.starts_with("lfo.step."))
    {
        const auto index = std::stoi(id.substr(9)) - 1;
        if (index >= 0 && index < synth::lfoStepSlotCount)
            parameters.lfo.steps[static_cast<std::size_t>(index)] = numeric;
    }
    else if (id == "direct.filter_keytrack") parameters.direct.filterKeytrack = numeric;
    else if (id == "direct.filter_lfo_semitones") parameters.direct.filterLfoSemitones = numeric;
    else if (id == "direct.filter_mod_env_semitones") parameters.direct.filterModEnvSemitones = numeric;
    else if (id == "direct.osc_keytrack_semitones") parameters.direct.oscKeytrackSemitones = numeric;
    else if (id == "direct.osc_lfo_semitones") parameters.direct.oscLfoSemitones = numeric;
    else if (id == "direct.osc_mod_env_semitones") parameters.direct.oscModEnvSemitones = numeric;
    else if (id == "fx.saturation_enabled") parameters.fx.saturationEnabled = numeric >= 0.5f;
    else if (id == "fx.distortion_mode") parameters.fx.distortionMode = static_cast<synth::DistortionMode>(choice);
    else if (id == "fx.saturation_mix") parameters.fx.saturationMix = numeric;
    else if (id == "fx.saturation_drive") parameters.fx.saturationDrive = numeric;
    else if (id == "fx.phaser_enabled") parameters.fx.phaserEnabled = numeric >= 0.5f;
    else if (id == "fx.phaser_mix") parameters.fx.phaserMix = numeric;
    else if (id == "fx.phaser_rate_hz") parameters.fx.phaserRateHz = numeric;
    else if (id == "fx.phaser_depth") parameters.fx.phaserDepth = numeric;
    else if (id == "fx.phaser_feedback") parameters.fx.phaserFeedback = numeric;
    else if (id == "fx.phaser_sync_division") parameters.fx.phaserSyncDivision = static_cast<synth::DelaySyncDivision>(choice);
    else if (id == "fx.delay_enabled") parameters.fx.delayEnabled = numeric >= 0.5f;
    else if (id == "fx.delay_mix") parameters.fx.delayMix = numeric;
    else if (id == "fx.delay_sync_division") parameters.fx.delaySyncDivision = static_cast<synth::DelaySyncDivision>(choice);
    else if (id == "fx.delay_feedback") parameters.fx.delayFeedback = numeric;
    else if (id == "fx.reverb_enabled") parameters.fx.reverbEnabled = numeric >= 0.5f;
    else if (id == "fx.reverb_mix") parameters.fx.reverbMix = numeric;
    else if (id == "fx.reverb_decay") parameters.fx.reverbDecay = numeric;
    else if (id == "fx.chorus_enabled") parameters.fx.chorusEnabled = numeric >= 0.5f;
    else if (id == "fx.chorus_mix") parameters.fx.chorusMix = numeric;
    else if (id == "fx.chorus_rate_hz") parameters.fx.chorusRateHz = numeric;
    else if (id == "fx.chorus_depth_ms") parameters.fx.chorusDepthMs = numeric;
    else if (id == "fx.chorus_sync_division") parameters.fx.chorusSyncDivision = static_cast<synth::DelaySyncDivision>(choice);
    else if (id == "fx.eq_enabled") parameters.fx.eqEnabled = numeric >= 0.5f;
    else if (id == "fx.eq_low_gain_db") parameters.fx.eqLowGainDb = numeric;
    else if (id == "fx.eq_high_gain_db") parameters.fx.eqHighGainDb = numeric;
    else if (id == "fx.compressor_enabled") parameters.fx.compressorEnabled = numeric >= 0.5f;
    else if (id == "fx.compressor_threshold_db") parameters.fx.compressorThresholdDb = numeric;
    else if (id == "fx.compressor_ratio") parameters.fx.compressorRatio = numeric;
    else if (id == "fx.compressor_makeup_db") parameters.fx.compressorMakeupDb = numeric;
    else if (id == "fx.compressor_mix") parameters.fx.compressorMix = numeric;
    else if (id == "fx.phaser_center_hz") parameters.fx.phaserCenterHz = numeric;
    else if (id == "fx.phaser_spread") parameters.fx.phaserSpread = numeric;
    else if (id == "fx.phaser_lr_offset") parameters.fx.phaserLrOffset = numeric;
    else if (id == "fx.phaser_width") parameters.fx.phaserWidth = numeric;
    else if (id == "fx.chorus_delay_ms") parameters.fx.chorusDelayMs = numeric;
    else if (id == "fx.chorus_feedback") parameters.fx.chorusFeedback = numeric;
    else if (id == "fx.chorus_width") parameters.fx.chorusWidth = numeric;
    else if (id == "fx.eq_low_frequency_hz") parameters.fx.eqLowFrequencyHz = numeric;
    else if (id == "fx.eq_high_frequency_hz") parameters.fx.eqHighFrequencyHz = numeric;
    else if (id == "fx.delay_time_left_ms") parameters.fx.delayTimeLeftMs = numeric;
    else if (id == "fx.delay_time_right_ms") parameters.fx.delayTimeRightMs = numeric;
    else if (id == "fx.delay_spread") parameters.fx.delaySpread = numeric;
    else if (id == "fx.delay_width") parameters.fx.delayWidth = numeric;
    else if (id == "fx.delay_low_cut_hz") parameters.fx.delayLowCutHz = numeric;
    else if (id == "fx.delay_high_cut_hz") parameters.fx.delayHighCutHz = numeric;
    else if (id == "fx.delay_smear") parameters.fx.delaySmear = numeric;
    else if (id == "fx.reverb_pre_delay_ms") parameters.fx.reverbPreDelayMs = numeric;
    else if (id == "fx.reverb_damp") parameters.fx.reverbDamp = numeric;
    else if (id == "fx.reverb_width") parameters.fx.reverbWidth = numeric;
    else if (id == "fx.compressor_attack_ms") parameters.fx.compressorAttackMs = numeric;
    else if (id == "fx.compressor_release_ms") parameters.fx.compressorReleaseMs = numeric;
    else if (id == "fx.chorus_dual_mode") parameters.fx.chorusDualMode = numeric >= 0.5f;
    else if (id == "fx.delay_ping_pong") parameters.fx.delayPingPong = numeric >= 0.5f;
    else if (id == "fx.delay_right_sync_division") parameters.fx.delayRightSyncDivision = static_cast<synth::DelaySyncDivision>(choice);
    else if (id == "ramp.enabled") parameters.ramp.enabled = numeric >= 0.5f;
    else if (id == "ramp.mode") parameters.ramp.mode = static_cast<synth::RampMode>(choice);
    else if (id == "ramp.delay_ms") parameters.ramp.delayMs = numeric;
    else if (id == "ramp.rise_ms") parameters.ramp.riseMs = numeric;
    else if (id == "ramp.curve") parameters.ramp.curve = static_cast<synth::RampCurve>(choice);
    else if (id == "arp.enabled") parameters.arp.enabled = numeric >= 0.5f;
    else if (id == "arp.mode") parameters.arp.mode = static_cast<synth::ArpMode>(choice);
    else if (id == "arp.rate") parameters.arp.rate = static_cast<synth::ArpRateDivision>(choice);
    else if (id == "arp.gate") parameters.arp.gate = numeric;
    else if (id == "arp.octaves") parameters.arp.octaves = static_cast<int>(std::round(numeric));
    else if (id == "arp.hold") parameters.arp.hold = numeric >= 0.5f;
    else if (id == "arp.swing") parameters.arp.swing = numeric;
    else if (id == "arp.step_count") parameters.arp.stepCount = static_cast<int>(std::round(numeric));
    else if (id == "arp.wrap") parameters.arp.wrap = static_cast<int>(std::round(numeric));
    else if (id == "arp.velocity_mode") parameters.arp.velocityMode = static_cast<synth::ArpVelocityMode>(choice);
    else if (id == "arp.time_ms") parameters.arp.timeMs = numeric;
    else if (id.starts_with("arp.step."))
    {
        const auto stepStart = std::string("arp.step.").size();
        const auto stepEnd = id.find('.', stepStart);
        if (stepEnd == std::string::npos)
            return;

        const auto stepNumber = std::stoi(id.substr(stepStart, stepEnd - stepStart));
        if (stepNumber < 1 || stepNumber > synth::arpStepCount)
            return;

        auto& step = parameters.arp.steps[static_cast<std::size_t>(stepNumber - 1)];
        const auto field = id.substr(stepEnd + 1);
        if (field == "enabled") step.enabled = numeric >= 0.5f;
        else if (field == "pitch_semitones") step.pitchSemitones = static_cast<int>(std::round(numeric));
        else if (field == "velocity") step.velocity = numeric;
        else if (field == "gate") step.gate = numeric;
        else if (field == "tie") step.tie = numeric >= 0.5f;
    }
    else if (id == "chord.enabled") parameters.chord.enabled = numeric >= 0.5f;
    else if (id == "chord.voice_count") parameters.chord.voiceCount = static_cast<int>(std::round(numeric));
    else if (id.starts_with("chord.voice."))
    {
        const auto voiceStart = std::string("chord.voice.").size();
        const auto voiceEnd = id.find('.', voiceStart);
        if (voiceEnd == std::string::npos)
            return;

        const auto voiceNumber = std::stoi(id.substr(voiceStart, voiceEnd - voiceStart));
        if (voiceNumber < 1 || voiceNumber > synth::chordVoiceCount)
            return;

        auto& voice = parameters.chord.voices[static_cast<std::size_t>(voiceNumber - 1)];
        const auto field = id.substr(voiceEnd + 1);
        if (field == "enabled") voice.enabled = numeric >= 0.5f;
        else if (field == "pitch_semitones") voice.pitchSemitones = static_cast<int>(std::round(numeric));
        else if (field == "velocity") voice.velocity = numeric;
    }
    else if (id == "macro.motion") parameters.macro.motion = numeric;
    else if (id == "macro.width") parameters.macro.width = numeric;
    else if (id == "macro.drive") parameters.macro.drive = numeric;
    else if (id == "macro.space") parameters.macro.space = numeric;
    else if (id.starts_with("layer."))
    {
        const auto layerStart = std::string("layer.").size();
        const auto layerEnd = id.find('.', layerStart);
        if (layerEnd == std::string::npos)
            return;

        const auto layerNumber = std::stoi(id.substr(layerStart, layerEnd - layerStart));
        if (layerNumber < 1 || layerNumber > synth::layerCount)
            return;

        auto& layer = parameters.layers[static_cast<std::size_t>(layerNumber - 1)];
        const auto field = id.substr(layerEnd + 1);
        if (field == "enabled") layer.enabled = numeric >= 0.5f;
        else if (field == "level_db") layer.levelDb = numeric;
        else if (field == "pan") layer.pan = numeric;
        else if (field == "solo") layer.solo = numeric >= 0.5f;
        else if (field == "mute") layer.mute = numeric >= 0.5f;
        else if (field == "filter.input") layer.input = static_cast<synth::FilterInput>(choice);
        else if (field.starts_with("filter.") || field.starts_with("amp_env."))
        {
            if (field == "filter.enabled") layer.filter.enabled = numeric >= 0.5f;
            else if (field == "filter.mode") layer.filter.mode = static_cast<synth::FilterMode>(choice);
            else if (field == "filter.cutoff_semitones") layer.filter.cutoffSemitones = numeric;
            else if (field == "filter.resonance") layer.filter.resonance = numeric;
            else if (field == "filter.drive") layer.filter.drive = numeric;
            else if (field == "filter.keytrack") layer.filter.keytrack = numeric;
            else if (field == "filter.warm_drive") layer.filter.warmDrive = numeric >= 0.5f;
            else if (field == "filter.oversampling") layer.filter.oversampling = choice;
            else if (field == "amp_env.attack_ms") layer.ampEnv.attackMs = numeric;
            else if (field == "amp_env.decay_ms") layer.ampEnv.decayMs = numeric;
            else if (field == "amp_env.sustain") layer.ampEnv.sustain = numeric;
            else if (field == "amp_env.release_ms") layer.ampEnv.releaseMs = numeric;
        }
        else if (field.starts_with("osc."))
        {
            const auto oscillatorStart = std::string("osc.").size();
            const auto oscillatorEnd = field.find('.', oscillatorStart);
            if (oscillatorEnd == std::string::npos)
                return;

            const auto oscillatorNumber = std::stoi(field.substr(oscillatorStart,
                                                                 oscillatorEnd - oscillatorStart));
            if (oscillatorNumber < 1 || oscillatorNumber > synth::oscillatorSlotsPerLayer)
                return;

            auto& oscillator = layer.oscillators[static_cast<std::size_t>(oscillatorNumber - 1)];
            const auto oscillatorField = field.substr(oscillatorEnd + 1);
            if (oscillatorField == "enabled") oscillator.enabled = numeric >= 0.5f;
            else if (oscillatorField == "voices") oscillator.voices = static_cast<int>(std::round(numeric));
            else if (oscillatorField == "waveform") oscillator.waveform = static_cast<synth::OscillatorSlotWaveform>(choice);
            else if (oscillatorField == "octave") oscillator.octave = static_cast<int>(std::round(numeric));
            else if (oscillatorField == "note") oscillator.note = static_cast<int>(std::round(numeric));
            else if (oscillatorField == "fine_cents") oscillator.fineCents = numeric;
            else if (oscillatorField == "level") oscillator.level = numeric;
            else if (oscillatorField == "phase_degrees") oscillator.phaseDegrees = numeric;
            else if (oscillatorField == "detune") oscillator.detune = numeric;
            else if (oscillatorField == "stereo") oscillator.stereo = numeric;
            else if (oscillatorField == "pan") oscillator.pan = numeric;
            else if (oscillatorField == "retrigger") oscillator.retrigger = numeric >= 0.5f;
            else if (oscillatorField == "invert") oscillator.invert = numeric >= 0.5f;
        }
    }
    else if (id.starts_with("transmod."))
    {
        const auto slotStart = std::string("transmod.").size();
        const auto slotEnd = id.find('.', slotStart);
        if (slotEnd == std::string::npos)
            return;

        const auto slotNumber = std::stoi(id.substr(slotStart, slotEnd - slotStart));
        if (slotNumber < 1 || slotNumber > synth::transModSlotCount)
            return;

        auto& slot = parameters.transMod.slots[static_cast<std::size_t>(slotNumber - 1)];
        const auto field = id.substr(slotEnd + 1);
        if (field == "enabled") slot.enabled = numeric >= 0.5f;
        else if (field == "source") slot.source = static_cast<synth::ModSource>(choice);
        else if (field == "scaler") slot.scaler = static_cast<synth::ModSource>(choice);
        else if (field == "depth") slot.depth = numeric;
        else if (field == "osc_pitch_semitones") slot.oscPitchSemitones = numeric;
        else if (field == "filter_cutoff_semitones") slot.filterCutoffSemitones = numeric;
        else if (field == "amp_level_db") slot.ampLevelDb = numeric;
        else if (field == "pan") slot.pan = numeric;
        else if (field.starts_with("native."))
        {
            for (const auto& destination : synth::modulationDestinationCatalog())
                if (destination.nativeIndex >= 0 && destination.depthSuffix == field)
                    slot.nativeDepths[static_cast<std::size_t>(destination.nativeIndex)] = numeric;
        }
    }
}

class RenderParameterOwner final : public juce::AudioProcessor
{
public:
    RenderParameterOwner()
        : parameters(*this, nullptr, "SYNTHIA_STATE", synth::createParameterLayout())
    {
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "RenderParameterOwner"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState parameters;
};

bool applyPreparedStateParameters(const juce::ValueTree& state,
                                  synth::SynthParameters& parameters,
                                  std::string& error)
{
    if (!state.isValid())
    {
        error = "prepared parameter state is invalid";
        return false;
    }

    for (const auto& child : state)
    {
        if (!child.hasType("PARAM"))
            continue;

        const auto id = child.getProperty("id").toString().toStdString();
        if (id.empty())
        {
            error = "prepared parameter state contains an empty parameter id";
            return false;
        }

        applyPresetValue(parameters, id, child.getProperty("value"));
    }

    const auto physicalValue = [&state](const std::string& id) {
        const auto parameter = state.getChildWithProperty("id", juce::String(id));
        return parameter.isValid() ? static_cast<float>(parameter.getProperty("value")) : 0.0f;
    };
    const auto& destinations = synth::modulationDestinationCatalog();
    for (int slotNumber = 1; slotNumber <= synth::transModSlotCount; ++slotNumber)
    {
        auto& slot = parameters.transMod.slots[static_cast<std::size_t>(slotNumber - 1)];
        for (int route = 1; route <= 2; ++route)
        {
            const auto prefix = "transmod." + std::to_string(slotNumber) + ".route." + std::to_string(route) + ".";
            const auto selected = static_cast<int>(physicalValue(prefix + "destination")) - 1;
            if (selected < 0 || selected >= static_cast<int>(destinations.size()))
                continue;
            slot.enabled = true;
            if (slotNumber <= 4)
            {
                constexpr std::array<synth::ModSource, 4> sources { synth::ModSource::ModEnv, synth::ModSource::ModEnv2,
                                                                    synth::ModSource::Lfo, synth::ModSource::Lfo2 };
                slot.source = sources[static_cast<std::size_t>(slotNumber - 1)];
            }
            const auto& destination = destinations[static_cast<std::size_t>(selected)];
            const auto scale = destination.destination == synth::ModulationDestination::FilterCutoff ? 72.0f : destination.maximumDepth;
            const auto amount = physicalValue(prefix + "amount") * scale;
            switch (destination.destination)
            {
                case synth::ModulationDestination::OscPitch: slot.oscPitchSemitones += amount; break;
                case synth::ModulationDestination::FilterCutoff: slot.filterCutoffSemitones += amount; break;
                case synth::ModulationDestination::AmpLevel: slot.ampLevelDb += amount; break;
                case synth::ModulationDestination::Pan: slot.pan += amount; break;
                case synth::ModulationDestination::Native: slot.nativeDepths[static_cast<std::size_t>(destination.nativeIndex)] += amount; break;
            }
        }
    }
    return true;
}

bool prepareRandomizedParameters(std::uint32_t seed,
                                 synth::SynthParameters& parameters,
                                 std::string& displayName,
                                 std::string& error)
{
    RenderParameterOwner owner;
    const auto result = synth::prepareRandomizedPresetState(owner.parameters, seed);
    if (!result.loaded)
    {
        error = result.message.empty() ? "randomized preset preparation failed" : result.message;
        return false;
    }

    parameters = {};
    displayName = result.displayName;
    return applyPreparedStateParameters(result.state, parameters, error);
}

bool loadPresetParameters(const std::filesystem::path& path, synth::SynthParameters& parameters, std::string& error)
{
    RenderParameterOwner owner;
    const auto prepared = synth::preparePresetState(owner.parameters, path);
    if (!prepared.loaded)
    {
        error = prepared.message.empty() ? "preset preparation failed: " + path.string() : prepared.message;
        return false;
    }
    parameters = {};
    return applyPreparedStateParameters(prepared.state, parameters, error);
}

bool loadMidiFixture(const std::filesystem::path& path, int sampleRate,
                     std::vector<RenderEvent>& events, std::string& error)
{
    if (!std::filesystem::is_regular_file(path))
    {
        error = "fixture file missing: " + path.string();
        return false;
    }

    juce::FileInputStream stream { juceFileForPath(path) };
    if (!stream.openedOk())
    {
        error = "fixture file could not be opened: " + path.string();
        return false;
    }

    juce::MidiFile midiFile;
    if (!midiFile.readFrom(stream))
    {
        error = "fixture is not a valid MIDI file: " + path.string();
        return false;
    }

    midiFile.convertTimestampTicksToSeconds();
    events.clear();

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        const auto* track = midiFile.getTrack(trackIndex);
        if (track == nullptr)
            continue;

        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
        {
            const auto* event = track->getEventPointer(eventIndex);
            if (event == nullptr)
                continue;

            auto message = event->message;
            if (!message.isNoteOnOrOff()
                && !message.isController()
                && !message.isPitchWheel()
                && !message.isAftertouch()
                && !message.isChannelPressure()
                && !message.isAllNotesOff()
                && !message.isAllSoundOff())
            {
                continue;
            }

            const auto sample = static_cast<int>(std::max(0.0, std::round(message.getTimeStamp() * sampleRate)));
            events.push_back({ sample, message });
        }
    }

    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.sample < b.sample;
    });

    const auto hasNote = std::any_of(events.begin(), events.end(), [](const auto& event) {
        return event.message.isNoteOnOrOff();
    });
    if (!hasNote)
    {
        error = "fixture contains no note events: " + path.string();
        return false;
    }

    return true;
}

void handleRenderMidi(synth::SynthEngine& engine, const juce::MidiMessage& message)
{
    if (message.isNoteOn(false))
        engine.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff(true))
        engine.noteOff(message.getNoteNumber());
    else if (message.isAllSoundOff())
        engine.panic();
    else if (message.isAllNotesOff())
        engine.allNotesOff();
    else if (message.isPitchWheel())
        engine.setPitchBend((static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f);
    else if (message.isControllerOfType(64))
        engine.setSustainPedal(message.getControllerValue() >= 64);
    else if (message.isControllerOfType(1))
        engine.setModWheel(static_cast<float>(message.getControllerValue()) / 127.0f);
    else if (message.isAftertouch())
        engine.setAftertouch(static_cast<float>(message.getAfterTouchValue()) / 127.0f);
    else if (message.isChannelPressure())
        engine.setAftertouch(static_cast<float>(message.getChannelPressureValue()) / 127.0f);
}

void writeLittleEndian16(std::ofstream& out, std::uint16_t value)
{
    out.put(static_cast<char>(value & 0xffu));
    out.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeLittleEndian32(std::ofstream& out, std::uint32_t value)
{
    out.put(static_cast<char>(value & 0xffu));
    out.put(static_cast<char>((value >> 8u) & 0xffu));
    out.put(static_cast<char>((value >> 16u) & 0xffu));
    out.put(static_cast<char>((value >> 24u) & 0xffu));
}

void writeWav16(const std::filesystem::path& path, const std::vector<float>& left,
                const std::vector<float>& right, int sampleRate)
{
    ensureParentDirectory(path);

    const auto sampleCount = std::min(left.size(), right.size());
    const auto dataBytes = static_cast<std::uint32_t>(sampleCount * 2u * sizeof(std::int16_t));

    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    writeLittleEndian32(out, 36u + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLittleEndian32(out, 16u);
    writeLittleEndian16(out, 1u);
    writeLittleEndian16(out, 2u);
    const auto byteRate = static_cast<std::uint32_t>(sampleRate) * 2u * static_cast<std::uint32_t>(sizeof(std::int16_t));
    writeLittleEndian32(out, static_cast<std::uint32_t>(sampleRate));
    writeLittleEndian32(out, byteRate);
    writeLittleEndian16(out, static_cast<std::uint16_t>(2 * sizeof(std::int16_t)));
    writeLittleEndian16(out, 16u);
    out.write("data", 4);
    writeLittleEndian32(out, dataBytes);

    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const auto l = static_cast<std::int16_t>(std::clamp(left[i], -1.0f, 1.0f) * 32767.0f);
        const auto r = static_cast<std::int16_t>(std::clamp(right[i], -1.0f, 1.0f) * 32767.0f);
        writeLittleEndian16(out, static_cast<std::uint16_t>(l));
        writeLittleEndian16(out, static_cast<std::uint16_t>(r));
    }
}

struct AudioMetrics
{
    float peak = 0.0f;
    double rms = 0.0;
    double rmsDbfs = -240.0;
    double crestDb = 0.0;
    double correlation = 0.0;
    double dcOffset = 0.0;
    double spectralCentroidHz = 0.0;
    int invalid = 0;
    int nonzero = 0;
};

double spectralCentroidHz(const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    const auto sampleCount = static_cast<int>(std::min<std::size_t>({ left.size(), right.size(), 4096u }));
    if (sampleCount < 32)
        return 0.0;

    constexpr auto maxBins = 256;
    double weighted = 0.0;
    double magnitudeSum = 0.0;

    for (int bin = 1; bin <= maxBins; ++bin)
    {
        double real = 0.0;
        double imag = 0.0;
        const auto radiansPerSample = -2.0 * juce::MathConstants<double>::pi * static_cast<double>(bin)
            / static_cast<double>(sampleCount);

        for (int i = 0; i < sampleCount; ++i)
        {
            const auto mono = 0.5 * (static_cast<double>(left[static_cast<std::size_t>(i)]) + static_cast<double>(right[static_cast<std::size_t>(i)]));
            const auto window = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * static_cast<double>(i) / static_cast<double>(sampleCount - 1));
            const auto angle = radiansPerSample * static_cast<double>(i);
            real += mono * window * std::cos(angle);
            imag += mono * window * std::sin(angle);
        }

        const auto magnitude = std::sqrt(real * real + imag * imag);
        const auto frequency = static_cast<double>(bin) * static_cast<double>(sampleRate)
            / static_cast<double>(sampleCount);
        weighted += frequency * magnitude;
        magnitudeSum += magnitude;
    }

    return magnitudeSum > 0.0 ? weighted / magnitudeSum : 0.0;
}

AudioMetrics analyzeAudio(const std::vector<float>& left, const std::vector<float>& right, int sampleRate = 48000)
{
    AudioMetrics metrics;
    double sumSquares = 0.0;
    double sumLeftSquares = 0.0;
    double sumRightSquares = 0.0;
    double sumCross = 0.0;
    double sumMono = 0.0;

    const auto sampleCount = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const auto l = left[i];
        const auto r = right[i];
        if (!std::isfinite(l) || !std::isfinite(r))
            ++metrics.invalid;

        metrics.peak = std::max(metrics.peak, std::max(std::abs(l), std::abs(r)));
        if (std::abs(l) > 1.0e-7f || std::abs(r) > 1.0e-7f)
            ++metrics.nonzero;

        sumSquares += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        sumLeftSquares += static_cast<double>(l) * l;
        sumRightSquares += static_cast<double>(r) * r;
        sumCross += static_cast<double>(l) * r;
        sumMono += 0.5 * (static_cast<double>(l) + static_cast<double>(r));
    }

    metrics.rms = sampleCount > 0 ? std::sqrt(sumSquares / static_cast<double>(sampleCount)) : 0.0;
    metrics.rmsDbfs = 20.0 * std::log10(std::max(metrics.rms, 1.0e-12));
    metrics.crestDb = metrics.rms > 0.0
        ? 20.0 * std::log10(std::max(static_cast<double>(metrics.peak), 1.0e-12) / metrics.rms)
        : 0.0;
    metrics.correlation = sumLeftSquares > 0.0 && sumRightSquares > 0.0
        ? sumCross / std::sqrt(sumLeftSquares * sumRightSquares)
        : 0.0;
    metrics.dcOffset = sampleCount > 0 ? sumMono / static_cast<double>(sampleCount) : 0.0;
    metrics.spectralCentroidHz = spectralCentroidHz(left, right, sampleRate);
    return metrics;
}

synth::VoiceSnapshot firstActiveSnapshot(const synth::SynthEngine& engine)
{
    for (int i = 0; i < 32; ++i)
    {
        const auto* voice = engine.getVoice(i);
        if (voice == nullptr)
            continue;

        const auto snapshot = voice->snapshot();
        if (snapshot.state != synth::VoiceState::Idle)
            return snapshot;
    }

    return {};
}

synth::RenderStats processSingleSamples(synth::SynthEngine& engine, int sampleCount)
{
    synth::RenderStats lastStats;
    float left = 0.0f;
    float right = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
        lastStats = engine.process(&left, &right, 1);
    return lastStats;
}

struct ModulationReportMetrics
{
    float rampHalfway = 0.0f;
    float rampEnd = 0.0f;
    bool rampTimingPassed = false;
    float glideStartNote = 0.0f;
    float glideEndNote = 0.0f;
    bool glidePassed = false;
    float velocityGlideStart = 0.0f;
    float velocityGlideEnd = 0.0f;
    bool velocityGlidePassed = false;
    float directOscPitch = 0.0f;
    float directFilter = 0.0f;
    bool directRoutesPassed = false;
    float transOscPitch = 0.0f;
    float transFilter = 0.0f;
    float transAmp = 0.0f;
    float transPan = 0.0f;
    bool transModScalerPassed = false;
    bool modSlotSchemaPassed = false;
    float performancePitch = 0.0f;
    float performanceWheel = 0.0f;
    float performanceAftertouch = 0.0f;
    bool performanceSourcesPassed = false;
    int fixtureEvents = 0;
    int fixtureMaxActiveVoices = 0;
    int fixtureInvalidSamples = 0;
    float fixtureRampMax = 0.0f;
    float fixtureVoiceUniRange = 0.0f;
    float fixtureVoiceBiRange = 0.0f;
    float fixtureUnisonUniRange = 0.0f;
    float fixtureUnisonBiRange = 0.0f;
    float fixtureRandomRange = 0.0f;
    bool fixtureSourcesPassed = false;
};

int writeModulationReport(const Options& options)
{
    constexpr auto sampleRate = 48000;
    ModulationReportMetrics metrics;

    {
        synth::SynthParameters parameters;
        parameters.voiceMode = synth::VoiceMode::MonoLegato;
        parameters.polyphony = 1;
        parameters.glideMs = 100.0f;
        parameters.velocityGlideMs = 100.0f;
        parameters.ramp.enabled = true;
        parameters.ramp.riseMs = 100.0f;
        parameters.ramp.curve = synth::RampCurve::Linear;
        parameters.filter.enabled = false;

        synth::SynthEngine engine;
        engine.prepare(sampleRate, 1);
        engine.setParameters(parameters);
        engine.noteOn(60, 0.25f);
        processSingleSamples(engine, sampleRate / 20);
        metrics.rampHalfway = firstActiveSnapshot(engine).ramp;

        engine.noteOn(72, 1.0f);
        processSingleSamples(engine, 16);
        auto snapshot = firstActiveSnapshot(engine);
        metrics.glideStartNote = snapshot.effectiveMidiNote;
        metrics.velocityGlideStart = snapshot.velocityGlide;

        processSingleSamples(engine, sampleRate / 10);
        snapshot = firstActiveSnapshot(engine);
        metrics.glideEndNote = snapshot.effectiveMidiNote;
        metrics.velocityGlideEnd = snapshot.velocityGlide;
        metrics.rampEnd = snapshot.ramp;
        metrics.rampTimingPassed = metrics.rampHalfway > 0.45f && metrics.rampHalfway < 0.55f && metrics.rampEnd > 0.98f;
        metrics.glidePassed = metrics.glideStartNote > 60.0f && metrics.glideStartNote < 61.0f
            && std::abs(metrics.glideEndNote - 72.0f) < 0.05f;
        metrics.velocityGlidePassed = metrics.velocityGlideStart > 0.25f && metrics.velocityGlideStart < 0.35f
            && std::abs(metrics.velocityGlideEnd - 1.0f) < 0.01f;
    }

    {
        synth::SynthParameters parameters;
        parameters.ramp.enabled = true;
        parameters.ramp.riseMs = 1.0f;
        parameters.direct.oscKeytrackSemitones = 12.0f;
        parameters.direct.filterKeytrack = 0.5f;
        parameters.macro.motion = 0.5f;
        parameters.filter.enabled = false;

        auto& slot = parameters.transMod.slots[0];
        slot.enabled = true;
        slot.source = synth::ModSource::Ramp;
        slot.scaler = synth::ModSource::Macro1;
        slot.oscPitchSemitones = 12.0f;
        slot.filterCutoffSemitones = 24.0f;
        slot.ampLevelDb = 6.0f;
        slot.pan = 0.4f;

        synth::SynthEngine engine;
        engine.prepare(sampleRate, 1);
        engine.setParameters(parameters);
        engine.noteOn(72, 1.0f);
        processSingleSamples(engine, 128);

        const auto snapshot = firstActiveSnapshot(engine);
        metrics.directOscPitch = snapshot.directOscPitchSemitones;
        metrics.directFilter = snapshot.directFilterCutoffSemitones;
        metrics.transOscPitch = snapshot.transModOscPitchSemitones;
        metrics.transFilter = snapshot.transModFilterCutoffSemitones;
        metrics.transAmp = snapshot.transModAmpLevelDb;
        metrics.transPan = snapshot.transModPan;
        metrics.directRoutesPassed = std::abs(metrics.directOscPitch - 12.0f) < 0.05f
            && std::abs(metrics.directFilter - 6.0f) < 0.05f;
        metrics.transModScalerPassed = std::abs(metrics.transOscPitch - 6.0f) < 0.05f
            && std::abs(metrics.transFilter - 12.0f) < 0.05f
            && std::abs(metrics.transAmp - 3.0f) < 0.05f
            && std::abs(metrics.transPan - 0.2f) < 0.01f;
    }

    {
        synth::SynthParameters parameters;
        std::string error;
        if (loadPresetParameters("fixtures/presets/mod-slots-only.SynthiaPreset", parameters, error))
        {
            const auto& slot = parameters.transMod.slots[0];
            metrics.modSlotSchemaPassed = slot.enabled
                && slot.source == synth::ModSource::Ramp
                && slot.scaler == synth::ModSource::Macro1
                && std::abs(slot.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::FilterACutoff)] - 12.0f) < 0.01f
                && std::abs(slot.nativeDepths[static_cast<std::size_t>(synth::NativeModDestination::LayerAPan)] - 0.25f) < 0.01f;
        }
    }

    {
        synth::SynthParameters parameters;
        parameters.filter.enabled = false;
        parameters.transMod.slots[0].enabled = true;
        parameters.transMod.slots[0].source = synth::ModSource::PitchBend;
        parameters.transMod.slots[0].oscPitchSemitones = 12.0f;
        parameters.transMod.slots[1].enabled = true;
        parameters.transMod.slots[1].source = synth::ModSource::ModWheel;
        parameters.transMod.slots[1].filterCutoffSemitones = 24.0f;
        parameters.transMod.slots[2].enabled = true;
        parameters.transMod.slots[2].source = synth::ModSource::Aftertouch;
        parameters.transMod.slots[2].ampLevelDb = 6.0f;

        synth::SynthEngine engine;
        engine.prepare(sampleRate, 1);
        engine.setParameters(parameters);
        engine.setPitchBend(0.5f);
        engine.setModWheel(0.25f);
        engine.setAftertouch(0.5f);
        engine.noteOn(60, 1.0f);
        processSingleSamples(engine, 64);

        const auto snapshot = firstActiveSnapshot(engine);
        metrics.performancePitch = snapshot.transModOscPitchSemitones;
        metrics.performanceWheel = snapshot.transModFilterCutoffSemitones;
        metrics.performanceAftertouch = snapshot.transModAmpLevelDb;
        metrics.performanceSourcesPassed = std::abs(metrics.performancePitch - 6.0f) < 0.05f
            && std::abs(metrics.performanceWheel - 6.0f) < 0.05f
            && std::abs(metrics.performanceAftertouch - 3.0f) < 0.05f;
    }

    std::vector<RenderEvent> events;
    std::string error;
    if (!loadMidiFixture(options.fixturePath, sampleRate, events, error))
    {
        std::cerr << error << "\n";
        return 1;
    }
    metrics.fixtureEvents = static_cast<int>(events.size());

    {
        synth::SynthParameters parameters;
        parameters.polyphony = 4;
        parameters.unisonCount = 2;
        parameters.ramp.enabled = true;
        parameters.ramp.riseMs = 60.0f;
        parameters.velocityGlideMs = 35.0f;
        parameters.glideMs = 35.0f;
        parameters.filter.enabled = false;
        parameters.osc.sawLevel = 1.0f;
        parameters.amp.panSpread = 0.25f;
        parameters.amp.unisonSpread = 0.25f;

        synth::SynthEngine engine;
        engine.prepare(sampleRate, 1);
        engine.setParameters(parameters);

        const auto lastEventSample = events.empty() ? 0 : events.back().sample;
        const auto sampleCount = lastEventSample + sampleRate / 2;
        auto nextEvent = std::size_t { 0 };
        auto minVoiceUni = 1.0f;
        auto maxVoiceUni = 0.0f;
        auto minVoiceBi = 1.0f;
        auto maxVoiceBi = -1.0f;
        auto minUnisonUni = 1.0f;
        auto maxUnisonUni = 0.0f;
        auto minUnisonBi = 1.0f;
        auto maxUnisonBi = -1.0f;
        auto minRandom = 1.0f;
        auto maxRandom = -1.0f;

        for (int sample = 0; sample < sampleCount; ++sample)
        {
            while (nextEvent < events.size() && events[nextEvent].sample == sample)
            {
                handleRenderMidi(engine, events[nextEvent].message);
                ++nextEvent;
            }

            const auto stats = processSingleSamples(engine, 1);
            metrics.fixtureInvalidSamples += stats.invalidSamples;
            metrics.fixtureMaxActiveVoices = std::max(metrics.fixtureMaxActiveVoices, stats.activeVoices);

            for (int voiceIndex = 0; voiceIndex < 32; ++voiceIndex)
            {
                const auto* voice = engine.getVoice(voiceIndex);
                if (voice == nullptr)
                    continue;

                const auto snapshot = voice->snapshot();
                if (snapshot.state == synth::VoiceState::Idle)
                    continue;

                metrics.fixtureRampMax = std::max(metrics.fixtureRampMax, snapshot.ramp);
                minVoiceUni = std::min(minVoiceUni, snapshot.voiceUni);
                maxVoiceUni = std::max(maxVoiceUni, snapshot.voiceUni);
                minVoiceBi = std::min(minVoiceBi, snapshot.voiceBi);
                maxVoiceBi = std::max(maxVoiceBi, snapshot.voiceBi);
                minUnisonUni = std::min(minUnisonUni, snapshot.unisonUni);
                maxUnisonUni = std::max(maxUnisonUni, snapshot.unisonUni);
                minUnisonBi = std::min(minUnisonBi, snapshot.unisonBi);
                maxUnisonBi = std::max(maxUnisonBi, snapshot.unisonBi);
                minRandom = std::min(minRandom, snapshot.randomOnNote);
                maxRandom = std::max(maxRandom, snapshot.randomOnNote);
            }
        }

        metrics.fixtureVoiceUniRange = maxVoiceUni - minVoiceUni;
        metrics.fixtureVoiceBiRange = maxVoiceBi - minVoiceBi;
        metrics.fixtureUnisonUniRange = maxUnisonUni - minUnisonUni;
        metrics.fixtureUnisonBiRange = maxUnisonBi - minUnisonBi;
        metrics.fixtureRandomRange = maxRandom - minRandom;
        metrics.fixtureSourcesPassed = metrics.fixtureInvalidSamples == 0
            && metrics.fixtureMaxActiveVoices >= 2
            && metrics.fixtureRampMax > 0.75f
            && metrics.fixtureVoiceUniRange > 0.1f
            && metrics.fixtureVoiceBiRange > 0.2f
            && metrics.fixtureUnisonUniRange > 0.9f
            && metrics.fixtureUnisonBiRange > 1.8f
            && metrics.fixtureRandomRange > 0.001f;
    }

    const auto passed = metrics.rampTimingPassed
        && metrics.glidePassed
        && metrics.velocityGlidePassed
        && metrics.directRoutesPassed
        && metrics.transModScalerPassed
        && metrics.modSlotSchemaPassed
        && metrics.performanceSourcesPassed
        && metrics.fixtureSourcesPassed;

    ensureParentDirectory(options.output);
    std::ofstream out(options.output);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"modulation\",\n";
    out << "  \"fixture\": \"" << options.fixturePath.string() << "\",\n";
    out << "  \"fixture_events\": " << metrics.fixtureEvents << ",\n";
    out << "  \"sample_rate\": " << sampleRate << ",\n";
    out << "  \"ramp_halfway\": " << metrics.rampHalfway << ",\n";
    out << "  \"ramp_end\": " << metrics.rampEnd << ",\n";
    out << "  \"ramp_timing_passed\": " << (metrics.rampTimingPassed ? "true" : "false") << ",\n";
    out << "  \"glide_start_note\": " << metrics.glideStartNote << ",\n";
    out << "  \"glide_end_note\": " << metrics.glideEndNote << ",\n";
    out << "  \"glide_passed\": " << (metrics.glidePassed ? "true" : "false") << ",\n";
    out << "  \"velocity_glide_start\": " << metrics.velocityGlideStart << ",\n";
    out << "  \"velocity_glide_end\": " << metrics.velocityGlideEnd << ",\n";
    out << "  \"velocity_glide_passed\": " << (metrics.velocityGlidePassed ? "true" : "false") << ",\n";
    out << "  \"direct_osc_pitch_semitones\": " << metrics.directOscPitch << ",\n";
    out << "  \"direct_filter_cutoff_semitones\": " << metrics.directFilter << ",\n";
    out << "  \"direct_routes_passed\": " << (metrics.directRoutesPassed ? "true" : "false") << ",\n";
    out << "  \"transmod_osc_pitch_semitones\": " << metrics.transOscPitch << ",\n";
    out << "  \"transmod_filter_cutoff_semitones\": " << metrics.transFilter << ",\n";
    out << "  \"transmod_amp_level_db\": " << metrics.transAmp << ",\n";
    out << "  \"transmod_pan\": " << metrics.transPan << ",\n";
    out << "  \"transmod_scaler_passed\": " << (metrics.transModScalerPassed ? "true" : "false") << ",\n";
    out << "  \"mod_slot_schema_passed\": " << (metrics.modSlotSchemaPassed ? "true" : "false") << ",\n";
    out << "  \"performance_pitchbend_osc_pitch\": " << metrics.performancePitch << ",\n";
    out << "  \"performance_modwheel_filter_cutoff\": " << metrics.performanceWheel << ",\n";
    out << "  \"performance_aftertouch_amp_level\": " << metrics.performanceAftertouch << ",\n";
    out << "  \"performance_sources_passed\": " << (metrics.performanceSourcesPassed ? "true" : "false") << ",\n";
    out << "  \"fixture_max_active_voices\": " << metrics.fixtureMaxActiveVoices << ",\n";
    out << "  \"fixture_invalid_samples\": " << metrics.fixtureInvalidSamples << ",\n";
    out << "  \"fixture_ramp_max\": " << metrics.fixtureRampMax << ",\n";
    out << "  \"fixture_voice_uni_range\": " << metrics.fixtureVoiceUniRange << ",\n";
    out << "  \"fixture_voice_bi_range\": " << metrics.fixtureVoiceBiRange << ",\n";
    out << "  \"fixture_unison_uni_range\": " << metrics.fixtureUnisonUniRange << ",\n";
    out << "  \"fixture_unison_bi_range\": " << metrics.fixtureUnisonBiRange << ",\n";
    out << "  \"fixture_random_range\": " << metrics.fixtureRandomRange << ",\n";
    out << "  \"fixture_sources_passed\": " << (metrics.fixtureSourcesPassed ? "true" : "false") << ",\n";
    out << "  \"passed\": " << (passed ? "true" : "false") << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

enum class PresetRenderVariant
{
    Default,
    PerVoiceLfo,
    MonoLfo
};

enum class RenderFxMode
{
    AsPrepared,
    Dry,
    Wet
};

const char* toString(PresetRenderVariant variant) noexcept
{
    switch (variant)
    {
        case PresetRenderVariant::Default: return "default";
        case PresetRenderVariant::PerVoiceLfo: return "per_voice_lfo";
        case PresetRenderVariant::MonoLfo: return "mono_lfo";
    }

    return "unknown";
}

const char* toString(RenderFxMode mode) noexcept
{
    switch (mode)
    {
        case RenderFxMode::AsPrepared: return "as_prepared";
        case RenderFxMode::Dry: return "dry";
        case RenderFxMode::Wet: return "wet";
    }

    return "unknown";
}

struct PresetRenderResult
{
    bool ok = false;
    std::string error;
    std::vector<float> left;
    std::vector<float> right;
    AudioMetrics metrics;
    int sampleRate = 48000;
    int sampleCount = 0;
    int fixtureEvents = 0;
    int lastEventSample = 0;
    int invalidDuringRender = 0;
    int tempoSyncedDelaySamples = 0;
    float delayDivisionBeats = 0.0f;
    float fxTailSeconds = 0.0f;
    float tempoBpm = 128.0f;
    float postLastEventSeconds = 0.0f;
    float noteLocalLfoSpread = 0.0f;
    bool noteLocalMotionPassed = false;
    double wetDryMaxAbsDiff = 0.0;
    double wetDryRmsDiff = 0.0;
    bool wetMeaningfulPassed = false;
    bool passed = false;
};

struct AudioDiff
{
    double maxAbs = 0.0;
    double rms = 0.0;
    double peakDelta = 0.0;
};

AudioDiff compareAudio(const PresetRenderResult& a, const PresetRenderResult& b);
void evaluateWetDifference(PresetRenderResult& wet, const PresetRenderResult& dry);
void writeFailureReport(const std::filesystem::path& reportPath,
                        const std::string& suite,
                        const std::string& error);

PresetRenderResult renderParameterAudio(synth::SynthParameters parameters,
                                        const std::filesystem::path& fixturePath,
                                        PresetRenderVariant variant,
                                        RenderFxMode fxMode = RenderFxMode::Dry,
                                        int blockSize = 1)
{
    PresetRenderResult result;
    blockSize = std::clamp(blockSize, 1, 8192);
    if (variant == PresetRenderVariant::MonoLfo)
    {
        parameters.lfo.mono = true;
        parameters.lfo.gateMode = synth::LfoGateMode::Mono;
    }
    else if (variant == PresetRenderVariant::PerVoiceLfo)
    {
        parameters.lfo.mono = false;
        parameters.lfo.gateMode = synth::LfoGateMode::PolyOn;
    }

    switch (fxMode)
    {
        case RenderFxMode::AsPrepared:
            break;
        case RenderFxMode::Dry:
            parameters.fx.enabled = false;
            break;
        case RenderFxMode::Wet:
            parameters.fx.enabled = true;
            break;
    }

    result.tempoBpm = parameters.tempoBpm;
    result.tempoSyncedDelaySamples = synth::tempoSyncedDelaySamples(result.sampleRate, parameters.tempoBpm,
                                                                    parameters.fx.delaySyncDivision);
    result.delayDivisionBeats = synth::delayDivisionBeats(parameters.fx.delaySyncDivision);
    result.fxTailSeconds = synth::fxTailLengthSeconds(parameters);

    std::vector<RenderEvent> events;
    if (!loadMidiFixture(fixturePath, result.sampleRate, events, result.error))
        return result;

    result.fixtureEvents = static_cast<int>(events.size());
    result.lastEventSample = events.empty() ? 0 : events.back().sample;
    const auto voiceTailSeconds = std::max({ parameters.layers[0].ampEnv.releaseMs, parameters.layers[1].ampEnv.releaseMs,
                                             parameters.modEnv.releaseMs, parameters.modEnv2.releaseMs })
        * 0.001f;
    result.postLastEventSeconds = std::max(0.4f, std::max(result.fxTailSeconds, voiceTailSeconds) + 0.1f);
    result.sampleCount = std::max(static_cast<int>(result.sampleRate * 1.35),
                                  result.lastEventSample
                                      + static_cast<int>(std::ceil(static_cast<double>(result.sampleRate)
                                                                   * result.postLastEventSeconds)));

    synth::SynthEngine engine;
    engine.prepare(result.sampleRate, blockSize);
    engine.setParameters(parameters);

    result.left.assign(static_cast<std::size_t>(result.sampleCount), 0.0f);
    result.right.assign(static_cast<std::size_t>(result.sampleCount), 0.0f);

    auto nextEvent = std::size_t { 0 };
    for (int i = 0; i < result.sampleCount;)
    {
        while (nextEvent < events.size() && events[nextEvent].sample == i)
        {
            handleRenderMidi(engine, events[nextEvent].message);
            ++nextEvent;
        }

        auto count = std::min(blockSize, result.sampleCount - i);
        if (nextEvent < events.size())
            count = std::min(count, events[nextEvent].sample - i);
        const auto stats = engine.process(&result.left[static_cast<std::size_t>(i)],
                                          &result.right[static_cast<std::size_t>(i)], count);
        if (stats.invalidSamples > 0)
        {
            result.invalidDuringRender += stats.invalidSamples;
            std::fill_n(result.left.data() + i, count, 0.0f);
            std::fill_n(result.right.data() + i, count, 0.0f);
        }

        auto activeVoiceCount = 0;
        auto firstNote = -1;
        auto hasMultipleNotes = false;
        auto minLfo = 1.0f;
        auto maxLfo = -1.0f;
        for (int voiceIndex = 0; voiceIndex < 32; ++voiceIndex)
        {
            const auto* voice = engine.getVoice(voiceIndex);
            if (voice == nullptr)
                continue;

            const auto snapshot = voice->snapshot();
            if (snapshot.state == synth::VoiceState::Idle)
                continue;

            if (firstNote < 0)
                firstNote = snapshot.midiNote;
            else if (snapshot.midiNote != firstNote)
                hasMultipleNotes = true;

            minLfo = std::min(minLfo, snapshot.lfo);
            maxLfo = std::max(maxLfo, snapshot.lfo);
            ++activeVoiceCount;
        }

        if (hasMultipleNotes && activeVoiceCount > 1)
            result.noteLocalLfoSpread = std::max(result.noteLocalLfoSpread, maxLfo - minLfo);
        i += count;
    }

    result.metrics = analyzeAudio(result.left, result.right, result.sampleRate);
    result.metrics.invalid += result.invalidDuringRender;
    result.noteLocalMotionPassed = result.noteLocalLfoSpread > 0.01f;

    const auto requiresNoteLocalMotion = variant != PresetRenderVariant::MonoLfo;
    result.passed = result.metrics.invalid == 0
        && result.metrics.peak < 1.0f
        && result.metrics.rms > 0.001
        && result.metrics.nonzero > result.sampleRate / 8
        && (!requiresNoteLocalMotion || result.noteLocalMotionPassed);
    result.ok = true;
    return result;
}

std::filesystem::path factoryPresetPathForId(const std::string& id)
{
    if (id == "init")
        return "presets/factory/Init/Init.SynthiaPreset";
    if (id == "pluck-core-01")
        return "presets/factory/Pluck/PL - Pluck Core 01.SynthiaPreset";
    if (id == "supersaw-stack-01")
        return "presets/factory/Lead/LD - Supersaw Stack 01.SynthiaPreset";
    if (id == "bass-wub-01")
        return "presets/factory/Bass/BA - Bass Wub 01.SynthiaPreset";
    if (id == "pad-wide-01")
        return "presets/factory/Pad/PD - Pad Wide 01.SynthiaPreset";
    if (id == "arp-motion-01")
        return "presets/factory/Arp/ARP - Arp Motion 01.SynthiaPreset";
    if (id == "fx-space-01")
        return "presets/factory/FX/FX - Space 01.SynthiaPreset";

    return std::filesystem::path { "presets/factory" } / (id + ".SynthiaPreset");
}

PresetRenderResult renderPresetAudio(const std::filesystem::path& presetPath,
                                     const std::filesystem::path& fixturePath,
                                     PresetRenderVariant variant,
                                     RenderFxMode fxMode = RenderFxMode::Dry)
{
    PresetRenderResult result;
    synth::SynthParameters parameters;
    if (!loadPresetParameters(presetPath, parameters, result.error))
        return result;

    return renderParameterAudio(parameters, fixturePath, variant, fxMode);
}

bool applyModulationRouteEditsToParameters(
    synth::SynthParameters& parameters,
    const std::vector<synth::ModulationRouteParameterEdit>& edits,
    std::string& error)
{
    for (const auto& edit : edits)
    {
        const auto* spec = synth::findParameterSpec(edit.parameterId);
        if (spec == nullptr)
        {
            error = "modulation route edit references unknown parameter: " + edit.parameterId;
            return false;
        }

        if (!std::isfinite(edit.value))
        {
            error = "modulation route edit contains non-finite value: " + edit.parameterId;
            return false;
        }

        const auto value = synth::clampPhysicalParameterValue(*spec, edit.value);
        applyPresetValue(parameters, edit.parameterId, juce::var(value));
    }

    return true;
}

bool hasExpectedRoute(const synth::ModulationRouteView& view)
{
    return std::any_of(view.activeRoutes.begin(), view.activeRoutes.end(), [](const auto& route) {
        return route.slotNumber == 8
            && route.sourceId == "macro.motion"
            && route.scalerId == "none"
            && route.destinationId == "amp.level"
            && std::abs(route.depth - 6.0f) < 0.001f
            && route.enabled;
    });
}

void writeRouteEditArray(std::ofstream& out,
                         const char* fieldName,
                         const std::vector<synth::ModulationRouteParameterEdit>& edits)
{
    out << "  \"" << fieldName << "\": [\n";
    for (std::size_t i = 0; i < edits.size(); ++i)
    {
        out << "    {\"parameter_id\": \"" << edits[i].parameterId << "\", ";
        out << "\"value\": " << edits[i].value << "}";
        out << (i + 1 == edits.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
}

int writeModulationRouteRenderReport(const Options& options)
{
    constexpr auto routedMaxAbsThreshold = 1.0e-4;
    constexpr auto routedRmsThreshold = 1.0e-5;
    constexpr auto clearMaxAbsTolerance = 1.0e-7;
    constexpr auto clearRmsTolerance = 1.0e-9;

    const auto presetPath = factoryPresetPathForId("pluck-core-01");
    synth::SynthParameters baselineParameters;
    std::string error;
    if (!loadPresetParameters(presetPath, baselineParameters, error))
    {
        writeFailureReport(options.output, "modulation-route-render", error);
        return 1;
    }

    const auto write = synth::buildModulationRouteWrite({ 8,
                                                          "macro.motion",
                                                          "none",
                                                          "amp.level",
                                                          6.0f });
    if (!write.ok)
    {
        writeFailureReport(options.output, "modulation-route-render", write.message);
        return 1;
    }

    auto routedParameters = baselineParameters;
    if (!applyModulationRouteEditsToParameters(routedParameters, write.edits, error))
    {
        writeFailureReport(options.output, "modulation-route-render", error);
        return 1;
    }

    const auto routeView = synth::buildModulationRouteView(routedParameters.transMod);
    const auto routeViewPassed = hasExpectedRoute(routeView);

    const auto clear = synth::buildModulationSlotClear(8);
    if (!clear.ok)
    {
        writeFailureReport(options.output, "modulation-route-render", clear.message);
        return 1;
    }

    auto clearedParameters = routedParameters;
    if (!applyModulationRouteEditsToParameters(clearedParameters, clear.edits, error))
    {
        writeFailureReport(options.output, "modulation-route-render", error);
        return 1;
    }

    const auto baseline = renderParameterAudio(baselineParameters, options.fixturePath,
                                               PresetRenderVariant::Default, RenderFxMode::Dry);
    const auto routed = renderParameterAudio(routedParameters, options.fixturePath,
                                             PresetRenderVariant::Default, RenderFxMode::Dry);
    const auto cleared = renderParameterAudio(clearedParameters, options.fixturePath,
                                              PresetRenderVariant::Default, RenderFxMode::Dry);
    if (!baseline.ok || !routed.ok || !cleared.ok)
    {
        const auto renderError = !baseline.ok ? baseline.error : (!routed.ok ? routed.error : cleared.error);
        writeFailureReport(options.output, "modulation-route-render", renderError);
        return 1;
    }

    const auto routedDiff = compareAudio(baseline, routed);
    const auto clearedDiff = compareAudio(baseline, cleared);
    const auto routedAudioChanged = routedDiff.maxAbs > routedMaxAbsThreshold
        && routedDiff.rms > routedRmsThreshold;
    const auto clearedAudioRestored = clearedDiff.maxAbs <= clearMaxAbsTolerance
        && clearedDiff.rms <= clearRmsTolerance;
    const auto renderPassed = baseline.passed && routed.passed && cleared.passed;
    const auto passed = routeViewPassed && renderPassed && routedAudioChanged && clearedAudioRestored;

    ensureParentDirectory(options.output);
    std::ofstream out(options.output);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"modulation-route-render\",\n";
    out << "  \"preset\": \"" << genericString(presetPath) << "\",\n";
    out << "  \"fixture\": \"" << genericString(options.fixturePath) << "\",\n";
    out << "  \"slot_number\": 8,\n";
    out << "  \"source_id\": \"macro.motion\",\n";
    out << "  \"scaler_id\": \"none\",\n";
    out << "  \"destination_id\": \"amp.level\",\n";
    out << "  \"depth_db\": 6.0,\n";
    out << "  \"route_write_message\": \"" << write.message << "\",\n";
    writeRouteEditArray(out, "route_edits", write.edits);
    writeRouteEditArray(out, "clear_edits", clear.edits);
    out << "  \"active_route_count\": " << routeView.activeRoutes.size() << ",\n";
    out << "  \"route_view_passed\": " << boolString(routeViewPassed) << ",\n";
    out << "  \"baseline_peak\": " << baseline.metrics.peak << ",\n";
    out << "  \"routed_peak\": " << routed.metrics.peak << ",\n";
    out << "  \"cleared_peak\": " << cleared.metrics.peak << ",\n";
    out << "  \"baseline_rms\": " << baseline.metrics.rms << ",\n";
    out << "  \"routed_rms\": " << routed.metrics.rms << ",\n";
    out << "  \"cleared_rms\": " << cleared.metrics.rms << ",\n";
    out << "  \"routed_max_abs_diff\": " << routedDiff.maxAbs << ",\n";
    out << "  \"routed_rms_diff\": " << routedDiff.rms << ",\n";
    out << "  \"routed_peak_delta\": " << routedDiff.peakDelta << ",\n";
    out << "  \"routed_max_abs_threshold\": " << routedMaxAbsThreshold << ",\n";
    out << "  \"routed_rms_threshold\": " << routedRmsThreshold << ",\n";
    out << "  \"routed_audio_changed\": " << boolString(routedAudioChanged) << ",\n";
    out << "  \"cleared_max_abs_diff\": " << clearedDiff.maxAbs << ",\n";
    out << "  \"cleared_rms_diff\": " << clearedDiff.rms << ",\n";
    out << "  \"cleared_peak_delta\": " << clearedDiff.peakDelta << ",\n";
    out << "  \"clear_max_abs_tolerance\": " << clearMaxAbsTolerance << ",\n";
    out << "  \"clear_rms_tolerance\": " << clearRmsTolerance << ",\n";
    out << "  \"cleared_audio_restored\": " << boolString(clearedAudioRestored) << ",\n";
    out << "  \"render_passed\": " << boolString(renderPassed) << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

void writePresetRenderReport(const std::filesystem::path& reportPath,
                             const PresetRenderResult& result,
                             const std::filesystem::path& presetPath,
                             const std::filesystem::path& fixturePath,
                             const std::filesystem::path& wavPath,
                             RenderFxMode fxMode,
                             PresetRenderVariant variant)
{
    ensureParentDirectory(reportPath);

    std::ofstream out(reportPath);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"preset-" << toString(fxMode) << "-render\",\n";
    out << "  \"variant\": \"" << toString(variant) << "\",\n";
    out << "  \"fx_mode\": \"" << toString(fxMode) << "\",\n";
    out << "  \"preset\": \"" << genericString(presetPath) << "\",\n";
    out << "  \"fixture\": \"" << genericString(fixturePath) << "\",\n";
    out << "  \"artifact_wav\": \"" << genericString(wavPath) << "\",\n";
    out << "  \"fixture_events\": " << result.fixtureEvents << ",\n";
    out << "  \"dry\": " << boolString(fxMode == RenderFxMode::Dry) << ",\n";
    out << "  \"wet\": " << boolString(fxMode == RenderFxMode::Wet) << ",\n";
    out << "  \"sample_rate\": " << result.sampleRate << ",\n";
    out << "  \"samples_rendered\": " << result.sampleCount << ",\n";
    out << "  \"last_event_sample\": " << result.lastEventSample << ",\n";
    out << "  \"post_last_event_seconds\": " << result.postLastEventSeconds << ",\n";
    out << "  \"tempo_bpm\": " << result.tempoBpm << ",\n";
    out << "  \"delay_division_beats\": " << result.delayDivisionBeats << ",\n";
    out << "  \"tempo_synced_delay_samples\": " << result.tempoSyncedDelaySamples << ",\n";
    out << "  \"fx_tail_seconds\": " << result.fxTailSeconds << ",\n";
    out << "  \"peak\": " << result.metrics.peak << ",\n";
    out << "  \"rms\": " << result.metrics.rms << ",\n";
    out << "  \"rms_dbfs\": " << result.metrics.rmsDbfs << ",\n";
    out << "  \"crest_db\": " << result.metrics.crestDb << ",\n";
    out << "  \"stereo_correlation\": " << result.metrics.correlation << ",\n";
    out << "  \"dc_offset\": " << result.metrics.dcOffset << ",\n";
    out << "  \"spectral_centroid_hz\": " << result.metrics.spectralCentroidHz << ",\n";
    out << "  \"note_local_lfo_spread\": " << result.noteLocalLfoSpread << ",\n";
    out << "  \"note_local_motion_passed\": " << boolString(result.noteLocalMotionPassed) << ",\n";
    out << "  \"wet_dry_max_abs_diff\": " << result.wetDryMaxAbsDiff << ",\n";
    out << "  \"wet_dry_rms_diff\": " << result.wetDryRmsDiff << ",\n";
    out << "  \"wet_meaningful_passed\": " << boolString(result.wetMeaningfulPassed) << ",\n";
    out << "  \"nonzero_samples\": " << result.metrics.nonzero << ",\n";
    out << "  \"invalid_samples\": " << result.metrics.invalid << ",\n";
    out << "  \"passed\": " << boolString(result.passed) << "\n";
    out << "}\n";
}

int renderPreset(const Options& options)
{
    const auto fxMode = options.wet && !options.dry ? RenderFxMode::Wet : RenderFxMode::Dry;
    auto result = renderPresetAudio(options.presetPath, options.fixturePath,
                                    PresetRenderVariant::Default, fxMode);
    if (!result.ok)
    {
        std::cerr << result.error << "\n";
        return 1;
    }

    if (fxMode == RenderFxMode::Wet)
    {
        const auto dryReference = renderPresetAudio(options.presetPath, options.fixturePath,
                                                    PresetRenderVariant::Default, RenderFxMode::Dry);
        if (!dryReference.ok)
        {
            std::cerr << dryReference.error << "\n";
            return 1;
        }

        evaluateWetDifference(result, dryReference);
    }

    writeWav16(options.output, result.left, result.right, result.sampleRate);
    writePresetRenderReport(options.report, result, options.presetPath, options.fixturePath,
                            options.output, fxMode, PresetRenderVariant::Default);
    return result.passed ? 0 : 1;
}

void writeFailureReport(const std::filesystem::path& reportPath, const std::string& suite, const std::string& error)
{
    ensureParentDirectory(reportPath);

    std::ofstream out(reportPath);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": " << jsonString(suite) << ",\n";
    out << "  \"error\": " << jsonString(error) << ",\n";
    out << "  \"passed\": false\n";
    out << "}\n";
}

int writeRandomizeReport(const Options& options)
{
    std::vector<RenderEvent> events;
    std::string error;
    if (!loadMidiFixture(options.fixturePath, 48000, events, error))
    {
        writeFailureReport(options.output, "randomize-render", error);
        return 1;
    }

    struct RandomizedCase
    {
        std::uint32_t seed = 0;
        std::string displayName;
        bool prepared = false;
        bool rendered = false;
        bool passed = false;
        bool fxEnabled = false;
        std::string error;
        PresetRenderResult render;
    };

    std::vector<RandomizedCase> cases;
    cases.reserve(options.randomizeSeeds.size());

    for (const auto seed : options.randomizeSeeds)
    {
        RandomizedCase item;
        item.seed = seed;

        synth::SynthParameters parameters;
        item.prepared = prepareRandomizedParameters(seed, parameters, item.displayName, item.error);
        if (item.prepared)
        {
            item.fxEnabled = parameters.fx.enabled;
            item.render = renderParameterAudio(parameters, options.fixturePath,
                                               PresetRenderVariant::Default, RenderFxMode::AsPrepared);
            item.rendered = item.render.ok;
            if (!item.render.ok)
                item.error = item.render.error;
        }

        item.passed = item.prepared
            && item.rendered
            && item.render.metrics.invalid == 0
            && item.render.metrics.peak < 1.0f
            && item.render.metrics.rms > 0.001
            && item.render.metrics.nonzero > item.render.sampleRate / 8;
        cases.push_back(std::move(item));
    }

    auto passedCount = 0;
    for (const auto& item : cases)
    {
        if (item.passed)
            ++passedCount;
    }

    ensureParentDirectory(options.output);
    std::ofstream out(options.output);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"randomize-render\",\n";
    out << "  \"fixture\": " << jsonString(options.fixturePath) << ",\n";
    out << "  \"seed_count\": " << cases.size() << ",\n";
    out << "  \"passed_count\": " << passedCount << ",\n";
    out << "  \"failed_count\": " << (static_cast<int>(cases.size()) - passedCount) << ",\n";
    out << "  \"cases\": [\n";
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& item = cases[i];
        out << "    {";
        out << "\"seed\": " << item.seed << ", ";
        out << "\"display_name\": " << jsonString(item.displayName) << ", ";
        out << "\"prepared\": " << boolString(item.prepared) << ", ";
        out << "\"rendered\": " << boolString(item.rendered) << ", ";
        out << "\"fx_enabled\": " << boolString(item.fxEnabled) << ", ";
        out << "\"peak\": " << item.render.metrics.peak << ", ";
        out << "\"rms\": " << item.render.metrics.rms << ", ";
        out << "\"nonzero_samples\": " << item.render.metrics.nonzero << ", ";
        out << "\"invalid_samples\": " << item.render.metrics.invalid << ", ";
        out << "\"error\": " << jsonString(item.error) << ", ";
        out << "\"passed\": " << boolString(item.passed);
        out << "}";
        out << (i + 1 == cases.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"passed\": " << boolString(passedCount == static_cast<int>(cases.size())) << "\n";
    out << "}\n";

    return passedCount == static_cast<int>(cases.size()) ? 0 : 1;
}

AudioDiff compareAudio(const PresetRenderResult& a, const PresetRenderResult& b)
{
    AudioDiff diff;
    const auto sampleCount = std::min(a.left.size(), b.left.size());
    auto sumSquares = 0.0;

    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const auto leftDiff = static_cast<double>(a.left[i]) - static_cast<double>(b.left[i]);
        const auto rightDiff = static_cast<double>(a.right[i]) - static_cast<double>(b.right[i]);
        diff.maxAbs = std::max(diff.maxAbs, std::max(std::abs(leftDiff), std::abs(rightDiff)));
        sumSquares += 0.5 * (leftDiff * leftDiff + rightDiff * rightDiff);
    }

    diff.rms = sampleCount > 0 ? std::sqrt(sumSquares / static_cast<double>(sampleCount)) : 0.0;
    diff.peakDelta = std::abs(static_cast<double>(a.metrics.peak) - static_cast<double>(b.metrics.peak));
    return diff;
}

void evaluateWetDifference(PresetRenderResult& wet, const PresetRenderResult& dry)
{
    const auto diff = compareAudio(wet, dry);
    wet.wetDryMaxAbsDiff = diff.maxAbs;
    wet.wetDryRmsDiff = diff.rms;
    wet.wetMeaningfulPassed = wet.ok && dry.ok
        && wet.metrics.invalid == 0 && dry.metrics.invalid == 0
        && diff.maxAbs > 1.0e-4
        && diff.rms > 1.0e-5;
    wet.passed = wet.passed && wet.wetMeaningfulPassed;
}

int writeWetDifferenceContractReport(const std::filesystem::path& reportPath,
                                     const std::filesystem::path& fixturePath)
{
    synth::SynthParameters parameters;
    parameters.fx.saturationEnabled = true;
    parameters.fx.saturationMix = 0.7f;
    parameters.fx.saturationDrive = 0.8f;
    parameters.fx.phaserEnabled = false;
    parameters.fx.chorusEnabled = false;
    parameters.fx.eqEnabled = false;
    parameters.fx.delayEnabled = false;
    parameters.fx.reverbEnabled = false;
    parameters.fx.compressorEnabled = false;
    const auto dry = renderParameterAudio(parameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Dry);
    auto wet = renderParameterAudio(parameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Wet);
    evaluateWetDifference(wet, dry);
    const auto audibleWithoutTimeBasedFx = wet.ok && dry.ok && wet.passed && wet.wetMeaningfulPassed;
    auto zeroDeclaredTail = wet;
    zeroDeclaredTail.fxTailSeconds = 0.0f;
    evaluateWetDifference(zeroDeclaredTail, dry);
    const auto zeroDeclaredTailPassed = zeroDeclaredTail.wetMeaningfulPassed;

    auto identical = dry;
    // A declared tail cannot substitute for an audible difference.
    identical.fxTailSeconds = 2.0f;
    evaluateWetDifference(identical, dry);
    const auto identicalRejected = !identical.wetMeaningfulPassed;

    parameters.fx.saturationEnabled = false;
    auto disabled = renderParameterAudio(parameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Wet);
    evaluateWetDifference(disabled, dry);
    const auto disabledRejected = disabled.ok && !disabled.wetMeaningfulPassed;

    const auto passed = audibleWithoutTimeBasedFx && zeroDeclaredTailPassed && identicalRejected && disabledRejected;
    std::ofstream out(reportPath);
    out << "{\n  \"schema_version\": 1,\n  \"suite\": \"wet-difference-contract\",\n";
    out << "  \"audible_without_delay_or_reverb_passed\": " << boolString(audibleWithoutTimeBasedFx) << ",\n";
    out << "  \"actual_fx_tail_seconds\": " << wet.fxTailSeconds << ",\n";
    out << "  \"zero_declared_tail_passed\": " << boolString(zeroDeclaredTailPassed) << ",\n";
    out << "  \"identical_with_declared_tail_rejected\": " << boolString(identicalRejected) << ",\n";
    out << "  \"disabled_effects_rejected\": " << boolString(disabledRejected) << ",\n";
    out << "  \"wet_dry_max_abs_diff\": " << wet.wetDryMaxAbsDiff << ",\n";
    out << "  \"wet_dry_rms_diff\": " << wet.wetDryRmsDiff << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n}\n";
    return passed ? 0 : 1;
}

int writeNativePresetLoadingContractReport(const std::filesystem::path& reportPath)
{
    synth::SynthParameters parameters;
    std::string error;
    if (!loadPresetParameters("tests/fixtures/native-loader-validation.SynthiaPreset", parameters, error))
    {
        writeFailureReport(reportPath, "native-preset-loading-contract", error);
        return 1;
    }
    synth::SynthEngine engine;
    engine.prepare(48000.0, 64);
    engine.setParameters(parameters);
    engine.noteOn(60, 1.0f);
    std::vector<float> left(12000);
    std::vector<float> right(12000);
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
    const auto measured = estimateFrequency(left, 48000.0);
    const auto expected = synth::midiNoteToHz(84.0f);
    const auto cents = measured > 0.0f ? std::abs(1200.0f * std::log2(measured / expected)) : 9999.0f;
    const auto metrics = analyzeAudio(left, right, 48000);
    const auto passed = stats.invalidSamples == 0
        && metrics.invalid == 0 && metrics.rms > 0.001 && metrics.peak < 1.0f && cents < 5.0f;
    std::ofstream out(reportPath);
    out << "{\n  \"schema_version\": 1,\n  \"suite\": \"native-preset-loading-contract\",\n";
    out << "  \"measured_hz\": " << measured << ",\n  \"expected_hz\": " << expected << ",\n";
    out << "  \"tuning_error_cents\": " << cents << ",\n  \"passed\": " << boolString(passed) << "\n}\n";
    return passed ? 0 : 1;
}

int writeNativeGlobalRoutingReport(const std::filesystem::path& reportPath,
                                   const std::filesystem::path& fixturePath)
{
    RenderParameterOwner owner;
    const auto set = [&owner](const char* id, float value) {
        auto* parameter = owner.parameters.getParameter(id);
        if (parameter == nullptr)
            return false;
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    };
    if (!set("fx.phaser_enabled", 1.0f) || !set("fx.phaser_mix", 0.7f)
        || !set("fx.phaser_center_hz", 1400.0f) || !set("master.level_db", -18.0f)
        || !set("macro.motion", 0.5f))
    {
        writeFailureReport(reportPath, "native-global-routing", "Native phaser parameter missing");
        return 1;
    }
    synth::SynthParameters baselineParameters;
    std::string error;
    if (!applyPreparedStateParameters(owner.parameters.copyState(), baselineParameters, error))
    {
        writeFailureReport(reportPath, "native-global-routing", error);
        return 1;
    }
    const auto route = synth::buildModulationRouteWrite({ 8, "macro.motion", "none", "fx.phaser_center_hz", 2000.0f });
    const auto clear = synth::buildModulationSlotClear(8);
    auto routedParameters = baselineParameters;
    if (!route.ok || !clear.ok || !applyModulationRouteEditsToParameters(routedParameters, route.edits, error))
    {
        writeFailureReport(reportPath, "native-global-routing", error.empty() ? "Phaser route compilation failed" : error);
        return 1;
    }
    auto clearedParameters = routedParameters;
    if (!applyModulationRouteEditsToParameters(clearedParameters, clear.edits, error))
    {
        writeFailureReport(reportPath, "native-global-routing", error);
        return 1;
    }
    const auto baseline = renderParameterAudio(baselineParameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Wet);
    const auto routed = renderParameterAudio(routedParameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Wet);
    const auto cleared = renderParameterAudio(clearedParameters, fixturePath, PresetRenderVariant::Default, RenderFxMode::Wet);
    const auto changed = compareAudio(baseline, routed);
    const auto restored = compareAudio(baseline, cleared);
    const auto baseUnchanged = routedParameters.fx.phaserCenterHz == baselineParameters.fx.phaserCenterHz;
    const auto passed = baseline.passed && routed.passed && cleared.passed && baseUnchanged
        && changed.maxAbs > 1.0e-4 && changed.rms > 1.0e-5 && restored.maxAbs <= 1.0e-7 && restored.rms <= 1.0e-9;
    std::ofstream out(reportPath);
    out << "{\n  \"schema_version\": 1,\n  \"suite\": \"native-global-routing\",\n";
    out.precision(std::numeric_limits<float>::max_digits10);
    out << "  \"baseline_phaser_center_hz\": " << baselineParameters.fx.phaserCenterHz << ",\n";
    out << "  \"routed_phaser_center_hz\": " << routedParameters.fx.phaserCenterHz << ",\n";
    out << "  \"visible_phaser_center_unchanged\": " << boolString(baseUnchanged) << ",\n";
    out << "  \"routed_max_abs_diff\": " << changed.maxAbs << ",\n  \"routed_rms_diff\": " << changed.rms << ",\n";
    out << "  \"cleared_max_abs_diff\": " << restored.maxAbs << ",\n  \"cleared_rms_diff\": " << restored.rms << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n}\n";
    return passed ? 0 : 1;
}

int writeOfflineRealtimeCompareReport(const Options& options)
{
    synth::SynthParameters parameters;
    std::string error;
    if (!loadPresetParameters(options.presetPath, parameters, error))
    {
        writeFailureReport(options.output, "offline-realtime-compare", error);
        return 1;
    }
    const auto realtime = renderParameterAudio(parameters, options.fixturePath,
                                               PresetRenderVariant::Default, RenderFxMode::AsPrepared, 64);
    const auto offline = renderParameterAudio(parameters, options.fixturePath,
                                              PresetRenderVariant::Default, RenderFxMode::AsPrepared, 512);
    if (!realtime.ok || !offline.ok)
    {
        writeFailureReport(options.output, "offline-realtime-compare", !realtime.ok ? realtime.error : offline.error);
        return 1;
    }
    const auto difference = compareAudio(offline, realtime);
    constexpr auto maxAbsoluteTolerance = 1.0e-7;
    constexpr auto rmsTolerance = 1.0e-9;
    const auto equivalent = realtime.left.size() == offline.left.size()
        && difference.maxAbs <= maxAbsoluteTolerance && difference.rms <= rmsTolerance;
    const auto bothFinite = realtime.passed && offline.passed;
    const auto passed = bothFinite && equivalent;
    ensureParentDirectory(options.output);
    std::ofstream out(options.output);
    out << "{\n  \"schema_version\": 2,\n  \"suite\": \"offline-realtime-compare\",\n";
    out << "  \"preset\": " << jsonString(options.presetPath) << ",\n";
    out << "  \"fixture\": " << jsonString(options.fixturePath) << ",\n";
    out << "  \"realtime_block_samples\": 64,\n  \"offline_block_samples\": 512,\n";
    out << "  \"realtime_peak\": " << realtime.metrics.peak << ",\n  \"offline_peak\": " << offline.metrics.peak << ",\n";
    out << "  \"max_abs_diff\": " << difference.maxAbs << ",\n  \"rms_diff\": " << difference.rms << ",\n";
    out << "  \"max_absolute_tolerance\": " << maxAbsoluteTolerance << ",\n  \"rms_tolerance\": " << rmsTolerance << ",\n";
    out << "  \"both_finite\": " << boolString(bothFinite) << ",\n  \"same_state_equivalent\": " << boolString(equivalent) << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n}\n";
    return passed ? 0 : 1;
}

int writeDeterminismReport(const std::filesystem::path& reportPath,
                           const std::filesystem::path& failureDir,
                           const std::filesystem::path& presetPath,
                           const std::filesystem::path& fixturePath)
{
    const auto first = renderPresetAudio(presetPath, fixturePath, PresetRenderVariant::Default);
    const auto second = renderPresetAudio(presetPath, fixturePath, PresetRenderVariant::Default);
    if (!first.ok || !second.ok)
    {
        writeFailureReport(reportPath, "determinism", !first.ok ? first.error : second.error);
        return 1;
    }

    const auto diff = compareAudio(first, second);
    constexpr auto maxAbsTolerance = 1.0e-7;
    constexpr auto rmsTolerance = 1.0e-9;
    constexpr auto peakTolerance = 1.0e-7;
    const auto passed = first.passed
        && second.passed
        && diff.maxAbs <= maxAbsTolerance
        && diff.rms <= rmsTolerance
        && diff.peakDelta <= peakTolerance;

    auto firstFailureWav = std::filesystem::path {};
    auto secondFailureWav = std::filesystem::path {};
    if (!passed)
    {
        ensureDirectory(failureDir);
        firstFailureWav = failureDir / "determinism-first.wav";
        secondFailureWav = failureDir / "determinism-second.wav";
        writeWav16(firstFailureWav, first.left, first.right, first.sampleRate);
        writeWav16(secondFailureWav, second.left, second.right, second.sampleRate);
    }

    ensureParentDirectory(reportPath);
    std::ofstream out(reportPath);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"determinism\",\n";
    out << "  \"preset\": \"" << genericString(presetPath) << "\",\n";
    out << "  \"fixture\": \"" << genericString(fixturePath) << "\",\n";
    out << "  \"max_abs_diff\": " << diff.maxAbs << ",\n";
    out << "  \"rms_diff\": " << diff.rms << ",\n";
    out << "  \"peak_delta\": " << diff.peakDelta << ",\n";
    out << "  \"max_abs_tolerance\": " << maxAbsTolerance << ",\n";
    out << "  \"rms_tolerance\": " << rmsTolerance << ",\n";
    out << "  \"peak_tolerance\": " << peakTolerance << ",\n";
    out << "  \"failure_first_wav\": \"" << genericString(firstFailureWav) << "\",\n";
    out << "  \"failure_second_wav\": \"" << genericString(secondFailureWav) << "\",\n";
    out << "  \"passed\": " << boolString(passed) << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

int writeLfoAblationReport(const std::filesystem::path& reportPath,
                           const std::filesystem::path& artifactDir,
                           const std::filesystem::path& presetPath,
                           const std::filesystem::path& fixturePath)
{
    const auto perVoice = renderPresetAudio(presetPath, fixturePath, PresetRenderVariant::PerVoiceLfo);
    const auto mono = renderPresetAudio(presetPath, fixturePath, PresetRenderVariant::MonoLfo);
    if (!perVoice.ok || !mono.ok)
    {
        writeFailureReport(reportPath, "lfo-ablation", !perVoice.ok ? perVoice.error : mono.error);
        return 1;
    }

    ensureDirectory(artifactDir);
    const auto perVoiceWav = artifactDir / "pluck-core-01-per-voice-lfo.wav";
    const auto monoWav = artifactDir / "pluck-core-01-mono-lfo.wav";
    writeWav16(perVoiceWav, perVoice.left, perVoice.right, perVoice.sampleRate);
    writeWav16(monoWav, mono.left, mono.right, mono.sampleRate);

    const auto diff = compareAudio(perVoice, mono);
    const auto lfoSpreadDelta = perVoice.noteLocalLfoSpread - mono.noteLocalLfoSpread;
    const auto rmsDbDelta = std::abs(perVoice.metrics.rmsDbfs - mono.metrics.rmsDbfs);
    const auto centroidDelta = std::abs(perVoice.metrics.spectralCentroidHz - mono.metrics.spectralCentroidHz);
    const auto passed = perVoice.passed
        && mono.passed
        && perVoice.noteLocalLfoSpread > 0.5f
        && mono.noteLocalLfoSpread < 0.01f
        && lfoSpreadDelta > 0.5f
        && diff.rms > 1.0e-5;

    ensureParentDirectory(reportPath);
    std::ofstream out(reportPath);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"lfo-ablation\",\n";
    out << "  \"preset\": \"" << genericString(presetPath) << "\",\n";
    out << "  \"fixture\": \"" << genericString(fixturePath) << "\",\n";
    out << "  \"per_voice_wav\": \"" << genericString(perVoiceWav) << "\",\n";
    out << "  \"mono_wav\": \"" << genericString(monoWav) << "\",\n";
    out << "  \"per_voice_lfo_spread\": " << perVoice.noteLocalLfoSpread << ",\n";
    out << "  \"mono_lfo_spread\": " << mono.noteLocalLfoSpread << ",\n";
    out << "  \"lfo_spread_delta\": " << lfoSpreadDelta << ",\n";
    out << "  \"rms_diff\": " << diff.rms << ",\n";
    out << "  \"max_abs_diff\": " << diff.maxAbs << ",\n";
    out << "  \"rms_db_delta\": " << rmsDbDelta << ",\n";
    out << "  \"spectral_centroid_delta_hz\": " << centroidDelta << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n";
    out << "}\n";

    return passed ? 0 : 1;
}

void writeCoreSummary(const std::filesystem::path& path,
                      const std::filesystem::path& outputDir,
                      const std::vector<SuiteItem>& items)
{
    ensureParentDirectory(path);

    auto passedCount = 0;
    for (const auto& item : items)
    {
        if (item.passed)
            ++passedCount;
    }

    const auto failedCount = static_cast<int>(items.size()) - passedCount;
    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"core\",\n";
    out << "  \"format\": \"standalone\",\n";
    out << "  \"output_dir\": \"" << genericString(outputDir) << "\",\n";
    out << "  \"fixture_id\": \"overlap-pluck\",\n";
    out << "  \"sample_rate\": 48000,\n";
    out << "  \"block_size\": 1,\n";
    out << "  \"tempo_bpm\": 128,\n";
    out << "  \"seed\": \"engine-default-deterministic\",\n";
    out << "  \"report_count\": " << items.size() << ",\n";
    out << "  \"passed_count\": " << passedCount << ",\n";
    out << "  \"failed_count\": " << failedCount << ",\n";
    out << "  \"reports\": [\n";
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        out << "    {\"name\": \"" << item.name << "\", ";
        out << "\"path\": \"" << genericString(item.reportPath) << "\", ";
        out << "\"passed\": " << boolString(item.passed) << "}";
        out << (i + 1 == items.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"passed\": " << boolString(failedCount == 0) << "\n";
    out << "}\n";
}

int writeCoreSuite(const Options& options)
{
    if (options.suite != "core")
    {
        std::cerr << "Unknown suite: " << options.suite << "\n";
        return 2;
    }

    const auto outputDir = options.outputDir;
    const auto artifactDir = outputDir / "artifacts";
    const auto failureDir = outputDir / "failures";
    const auto presetPath = factoryPresetPathForId("pluck-core-01");
    const auto presetDirectory = std::filesystem::path { "presets/factory" };
    const auto fixturePath = options.fixturePath;

    ensureDirectory(outputDir);
    ensureDirectory(artifactDir);

    std::vector<SuiteItem> items;
    auto addItem = [&items](std::string name, std::filesystem::path reportPath, int exitCode) {
        items.push_back({ std::move(name), std::move(reportPath), exitCode == 0 });
    };

    {
        synth::SynthEngine engine;
        engine.prepare(48000.0, 256);
        std::vector<float> left(4800);
        std::vector<float> right(4800);
        const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));
        const auto reportPath = outputDir / "smoke.json";
        writeSmokeReport(reportPath, stats);
        addItem("smoke", reportPath, stats.invalidSamples == 0 ? 0 : 1);
    }

    {
        const auto reportPath = outputDir / "parameters.json";
        writeParameterReport(reportPath);
        addItem("parameters", reportPath, synth::validateParameterSpecs().empty() ? 0 : 1);
    }

    {
        const auto reportPath = outputDir / "presets.json";
        addItem("presets", reportPath, writePresetReport(reportPath, presetDirectory));
    }

    {
        const auto reportPath = outputDir / "voice-core.json";
        addItem("voice-core", reportPath, writeVoiceReport(reportPath));
    }

    {
        const auto reportPath = outputDir / "oscillator.json";
        addItem("oscillator", reportPath, writeOscillatorReport(reportPath, options.notes));
    }

    {
        const auto reportPath = outputDir / "filter.json";
        addItem("filter", reportPath, writeFilterReport(reportPath));
    }

    {
        auto modulationOptions = options;
        modulationOptions.output = outputDir / "modulation.json";
        addItem("modulation", modulationOptions.output, writeModulationReport(modulationOptions));
    }

    {
        auto routeRenderOptions = options;
        routeRenderOptions.output = outputDir / "modulation-route-render.json";
        addItem("modulation-route-render", routeRenderOptions.output,
                writeModulationRouteRenderReport(routeRenderOptions));
    }

    {
        const auto reportPath = outputDir / "native-preset-loading-contract.json";
        addItem("native-preset-loading-contract", reportPath, writeNativePresetLoadingContractReport(reportPath));
    }

    {
        const auto reportPath = outputDir / "native-global-routing.json";
        addItem("native-global-routing", reportPath, writeNativeGlobalRoutingReport(reportPath, fixturePath));
    }

    {
        const auto reportPath = outputDir / "wet-difference-contract.json";
        addItem("wet-difference-contract", reportPath, writeWetDifferenceContractReport(reportPath, fixturePath));
    }

    {
        auto compareOptions = options;
        compareOptions.output = outputDir / "offline-realtime-compare.json";
        compareOptions.presetPath = factoryPresetPathForId("fx-space-01");
        addItem("offline-realtime-compare", compareOptions.output,
                writeOfflineRealtimeCompareReport(compareOptions));
    }

    {
        auto randomizeOptions = options;
        randomizeOptions.output = outputDir / "randomize.json";
        addItem("randomize-render", randomizeOptions.output, writeRandomizeReport(randomizeOptions));
    }

    {
        const auto reportPath = outputDir / "pluck-core-01-dry.json";
        const auto wavPath = artifactDir / "pluck-core-01-dry.wav";
        const auto render = renderPresetAudio(presetPath, fixturePath, PresetRenderVariant::Default);
        if (!render.ok)
        {
            writeFailureReport(reportPath, "preset-dry-render", render.error);
            addItem("pluck-core-01-dry", reportPath, 1);
        }
        else
        {
            writeWav16(wavPath, render.left, render.right, render.sampleRate);
            writePresetRenderReport(reportPath, render, presetPath, fixturePath, wavPath,
                                    RenderFxMode::Dry, PresetRenderVariant::Default);
            addItem("pluck-core-01-dry", reportPath, render.passed ? 0 : 1);
        }
    }

    {
        const auto reportPath = outputDir / "pluck-core-01-wet.json";
        const auto wavPath = artifactDir / "pluck-core-01-wet.wav";
        const auto render = renderPresetAudio(presetPath, fixturePath,
                                              PresetRenderVariant::Default,
                                              RenderFxMode::Wet);
        if (!render.ok)
        {
            writeFailureReport(reportPath, "preset-wet-render", render.error);
            addItem("pluck-core-01-wet", reportPath, 1);
        }
        else
        {
            const auto dryReference = renderPresetAudio(presetPath, fixturePath,
                                                        PresetRenderVariant::Default,
                                                        RenderFxMode::Dry);
            auto checkedRender = render;
            if (dryReference.ok)
                evaluateWetDifference(checkedRender, dryReference);
            else
                checkedRender.passed = false;

            writeWav16(wavPath, render.left, render.right, render.sampleRate);
            writePresetRenderReport(reportPath, checkedRender, presetPath, fixturePath, wavPath,
                                    RenderFxMode::Wet, PresetRenderVariant::Default);
            const auto expectedDelaySamples = synth::tempoSyncedDelaySamples(checkedRender.sampleRate,
                                                                             checkedRender.tempoBpm,
                                                                             synth::DelaySyncDivision::Eighth);
            const auto wetPassed = dryReference.ok
                && checkedRender.passed
                && checkedRender.fxTailSeconds > 0.5f
                && checkedRender.wetMeaningfulPassed
                && checkedRender.postLastEventSeconds >= checkedRender.fxTailSeconds
                && std::abs(checkedRender.tempoSyncedDelaySamples - expectedDelaySamples) <= 1;
            addItem("pluck-core-01-wet", reportPath, wetPassed ? 0 : 1);
        }
    }

    {
        const auto reportPath = outputDir / "lfo-ablation.json";
        addItem("lfo-ablation", reportPath, writeLfoAblationReport(reportPath, artifactDir, presetPath, fixturePath));
    }

    {
        const auto reportPath = outputDir / "determinism.json";
        addItem("determinism", reportPath, writeDeterminismReport(reportPath, failureDir, presetPath, fixturePath));
    }

    const auto summaryPath = outputDir / "summary.json";
    writeCoreSummary(summaryPath, outputDir, items);

    const auto failed = std::count_if(items.begin(), items.end(), [](const auto& item) {
        return !item.passed;
    });

    std::cout << "Core suite wrote " << items.size() << " reports to " << outputDir << ".\n";
    return failed == 0 ? 0 : 1;
}

struct PatchRecreationCase
{
    const char* id = "";
    const char* displayName = "";
    const char* category = "";
    const char* intent = "";
    RenderFxMode fxMode = RenderFxMode::Wet;
    PresetRenderVariant variant = PresetRenderVariant::Default;
    bool requireWetProof = true;
    bool requireArpChordStateProof = false;
};

struct PatchRecreationItem
{
    PatchRecreationCase patch;
    std::filesystem::path presetPath;
    std::filesystem::path reportPath;
    std::filesystem::path wavPath;
    PresetRenderResult result;
    bool arpChordStatePassed = true;
    bool passed = false;
};

std::vector<PatchRecreationCase> patchRecreationCases()
{
    return {
        { "pluck-core-01", "Pluck Core 01", "Plucks",
          "Existing pluck baseline with ramp, TransMod, saturation, delay, and reverb.",
          RenderFxMode::Wet, PresetRenderVariant::Default, true, false },
        { "supersaw-stack-01", "Supersaw Stack 01", "Lead",
          "Wide saw lead using one primary saw stack, restrained detune, chorus, delay, and reverb.",
          RenderFxMode::Wet, PresetRenderVariant::Default, true, false },
        { "bass-wub-01", "Bass Wub 01", "Bass",
          "Mono-legato bass with sub support, filter LFO motion, drive, and compressor proof.",
          RenderFxMode::Wet, PresetRenderVariant::MonoLfo, true, false },
        { "pad-wide-01", "Pad Wide 01", "Pad",
          "Slow-attack pad with a compact saw stack, chorus, delay, and reverb.",
          RenderFxMode::Wet, PresetRenderVariant::Default, true, false },
        { "arp-motion-01", "Arp Motion 01", "Arp",
          "Arpeggiated chord/step patch proving preset-loaded arp and chord state in the renderer.",
          RenderFxMode::Wet, PresetRenderVariant::Default, true, true },
        { "fx-space-01", "FX Space 01", "FX",
          "FX-forward pluck that exercises the expanded rack tail and wet-versus-dry comparison.",
          RenderFxMode::Wet, PresetRenderVariant::Default, true, false }
    };
}

bool arpChordPresetStateMatches(const std::filesystem::path& presetPath)
{
    synth::SynthParameters parameters;
    std::string error;
    if (!loadPresetParameters(presetPath, parameters, error))
        return false;

    const auto& arp = parameters.arp;
    const auto& chord = parameters.chord;
    return arp.enabled
        && arp.mode == synth::ArpMode::UpDown
        && arp.rate == synth::ArpRateDivision::Sixteenth
        && std::abs(arp.gate - 0.58f) < 0.001f
        && arp.octaves == 2
        && std::abs(arp.swing - 0.18f) < 0.001f
        && arp.stepCount == 8
        && arp.steps[1].pitchSemitones == 7
        && std::abs(arp.steps[1].velocity - 0.82f) < 0.001f
        && arp.steps[4].tie
        && chord.enabled
        && chord.voiceCount == 3
        && chord.voices[1].enabled
        && chord.voices[1].pitchSemitones == 7
        && std::abs(chord.voices[1].velocity - 0.72f) < 0.001f
        && chord.voices[2].enabled
        && chord.voices[2].pitchSemitones == 12;
}

void writePatchRecreationSummary(const std::filesystem::path& path,
                                 const std::filesystem::path& outputDir,
                                 const std::filesystem::path& fixturePath,
                                 const std::vector<PatchRecreationItem>& items)
{
    ensureParentDirectory(path);

    auto passedCount = 0;
    for (const auto& item : items)
    {
        if (item.passed)
            ++passedCount;
    }

    const auto failedCount = static_cast<int>(items.size()) - passedCount;
    std::ofstream out(path);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"patch-recreation\",\n";
    out << "  \"format\": \"standalone\",\n";
    out << "  \"output_dir\": \"" << genericString(outputDir) << "\",\n";
    out << "  \"fixture\": \"" << genericString(fixturePath) << "\",\n";
    out << "  \"fixture_id\": \"overlap-pluck\",\n";
    out << "  \"sample_rate\": 48000,\n";
    out << "  \"block_size\": 1,\n";
    out << "  \"tempo_bpm\": 128,\n";
    out << "  \"seed\": \"engine-default-deterministic\",\n";
    out << "  \"preset_count\": " << items.size() << ",\n";
    out << "  \"report_count\": " << items.size() << ",\n";
    out << "  \"passed_count\": " << passedCount << ",\n";
    out << "  \"failed_count\": " << failedCount << ",\n";
    out << "  \"patches\": [\n";
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        out << "    {";
        out << "\"id\": \"" << item.patch.id << "\", ";
        out << "\"display_name\": \"" << item.patch.displayName << "\", ";
        out << "\"category\": \"" << item.patch.category << "\", ";
        out << "\"intent\": \"" << item.patch.intent << "\", ";
        out << "\"preset\": \"" << genericString(item.presetPath) << "\", ";
        out << "\"report\": \"" << genericString(item.reportPath) << "\", ";
        out << "\"artifact_wav\": \"" << genericString(item.wavPath) << "\", ";
        out << "\"fx_mode\": \"" << toString(item.patch.fxMode) << "\", ";
        out << "\"variant\": \"" << toString(item.patch.variant) << "\", ";
        out << "\"peak\": " << item.result.metrics.peak << ", ";
        out << "\"rms_dbfs\": " << item.result.metrics.rmsDbfs << ", ";
        out << "\"spectral_centroid_hz\": " << item.result.metrics.spectralCentroidHz << ", ";
        out << "\"note_local_lfo_spread\": " << item.result.noteLocalLfoSpread << ", ";
        out << "\"wet_dry_rms_diff\": " << item.result.wetDryRmsDiff << ", ";
        out << "\"wet_meaningful_passed\": " << boolString(item.result.wetMeaningfulPassed) << ", ";
        out << "\"arp_chord_state_passed\": " << boolString(item.arpChordStatePassed) << ", ";
        out << "\"passed\": " << boolString(item.passed);
        out << "}";
        out << (i + 1 == items.size() ? "\n" : ",\n");
    }
    out << "  ],\n";
    out << "  \"passed\": " << boolString(failedCount == 0) << "\n";
    out << "}\n";
}

int writePatchRecreationSuite(const Options& options)
{
    const auto outputDir = options.outputDir;
    const auto artifactDir = outputDir / "artifacts";
    const auto fixturePath = options.fixturePath;

    ensureDirectory(outputDir);
    ensureDirectory(artifactDir);

    std::vector<PatchRecreationItem> items;
    for (const auto& patch : patchRecreationCases())
    {
        const auto presetPath = factoryPresetPathForId(patch.id);
        const auto patchFixture = patch.id == "pad-wide-01" && fixturePath == std::filesystem::path("fixtures/midi/overlap-pluck.mid")
            ? std::filesystem::path("fixtures/midi/held-pad-triad.mid")
            : fixturePath;
        const auto reportPath = outputDir / (std::string(patch.id) + "-" + toString(patch.fxMode) + ".json");
        const auto wavPath = artifactDir / (std::string(patch.id) + "-" + toString(patch.fxMode) + ".wav");

        auto render = renderPresetAudio(presetPath, patchFixture, patch.variant, patch.fxMode);
        const auto arpChordStatePassed = !patch.requireArpChordStateProof
            || arpChordPresetStateMatches(presetPath);
        auto passed = false;

        if (!render.ok)
        {
            writeFailureReport(reportPath, "patch-recreation", render.error);
        }
        else
        {
            if (patch.fxMode == RenderFxMode::Wet)
            {
                const auto dryReference = renderPresetAudio(presetPath, patchFixture,
                                                            patch.variant, RenderFxMode::Dry);
                if (dryReference.ok)
                {
                    evaluateWetDifference(render, dryReference);
                }
                else
                {
                    render.passed = false;
                    render.error = dryReference.error;
                }
            }

            writeWav16(wavPath, render.left, render.right, render.sampleRate);
            writePresetRenderReport(reportPath, render, presetPath, patchFixture, wavPath,
                                    patch.fxMode, patch.variant);
            passed = render.passed
                && (!patch.requireWetProof || render.wetMeaningfulPassed)
                && arpChordStatePassed;
        }

        items.push_back({ patch, presetPath, reportPath, wavPath, std::move(render),
                          arpChordStatePassed, passed });
    }

    const auto summaryPath = outputDir / "summary.json";
    writePatchRecreationSummary(summaryPath, outputDir, fixturePath, items);

    const auto failed = std::count_if(items.begin(), items.end(), [](const auto& item) {
        return !item.passed;
    });

    std::cout << "Patch recreation suite wrote " << items.size() << " reports to "
              << outputDir << ".\n";
    return failed == 0 ? 0 : 1;
}
std::filesystem::path benchPresetPath(const std::string& idOrPath)
{
    if (idOrPath.find('/') != std::string::npos
        || idOrPath.find(".SynthiaPreset") != std::string::npos)
    {
        return idOrPath;
    }

    return factoryPresetPathForId(idOrPath);
}

int runPerfBench(const Options& options)
{
    const auto presetPath = benchPresetPath(options.benchPreset);
    synth::SynthParameters parameters;
    std::string error;
    if (!loadPresetParameters(presetPath, parameters, error))
    {
        std::cerr << "bench: " << error << "\n";
        return 2;
    }

    const auto notes = parseNotes(options.benchNotes);
    if (notes.empty())
    {
        std::cerr << "bench: --bench-notes produced no notes\n";
        return 2;
    }

    constexpr auto sampleRate = 48000.0;
    const auto blockSize = std::clamp(options.benchBlock, 16, 4096);
    const auto benchSeconds = std::clamp(options.benchSeconds, 0.5, 120.0);
    const auto reps = std::clamp(options.benchReps, 1, 50);
    const auto totalSamples = static_cast<int>(sampleRate * benchSeconds);
    const auto warmupSamples = static_cast<int>(sampleRate * 1.5);

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.0f);

    struct RepResult
    {
        double wallMs = 0.0;
        double realtimeMultiple = 0.0;
        int activeVoices = 0;
        float peak = 0.0f;
        int invalidSamples = 0;
    };

    std::vector<RepResult> repResults;
    repResults.reserve(static_cast<std::size_t>(reps));

    for (int rep = 0; rep < reps; ++rep)
    {
        synth::SynthEngine engine;
        engine.prepare(sampleRate, blockSize);
        engine.setParameters(parameters);
        for (const auto note : notes)
            engine.noteOn(note, 0.9f);

        for (int rendered = 0; rendered < warmupSamples; rendered += blockSize)
        {
            engine.setParameters(parameters);
            engine.process(left.data(), right.data(),
                           std::min(blockSize, warmupSamples - rendered));
        }

        RepResult result;
        const auto start = std::chrono::steady_clock::now();
        for (int rendered = 0; rendered < totalSamples; rendered += blockSize)
        {
            const auto blockSamples = std::min(blockSize, totalSamples - rendered);
            engine.setParameters(parameters);
            const auto stats = engine.process(left.data(), right.data(), blockSamples);
            result.peak = std::max(result.peak, stats.peak);
            result.invalidSamples += stats.invalidSamples;
            result.activeVoices = stats.activeVoices;
        }
        const auto end = std::chrono::steady_clock::now();

        const auto wallSeconds = std::chrono::duration<double>(end - start).count();
        result.wallMs = wallSeconds * 1000.0;
        result.realtimeMultiple = wallSeconds > 0.0
            ? (static_cast<double>(totalSamples) / sampleRate) / wallSeconds
            : 0.0;
        repResults.push_back(result);
    }

    auto sortedWallMs = std::vector<double> {};
    for (const auto& rep : repResults)
        sortedWallMs.push_back(rep.wallMs);
    std::sort(sortedWallMs.begin(), sortedWallMs.end());
    const auto bestWallMs = sortedWallMs.front();
    const auto medianWallMs = sortedWallMs[sortedWallMs.size() / 2];
    const auto audioSeconds = static_cast<double>(totalSamples) / sampleRate;
    const auto bestRealtime = bestWallMs > 0.0 ? audioSeconds * 1000.0 / bestWallMs : 0.0;
    const auto medianRealtime = medianWallMs > 0.0 ? audioSeconds * 1000.0 / medianWallMs : 0.0;

    const auto& last = repResults.back();
    auto totalInvalid = 0;
    for (const auto& rep : repResults)
        totalInvalid += rep.invalidSamples;

    const auto loadValid = last.activeVoices > 0 && last.peak > 0.0001f;
    const auto passed = totalInvalid == 0 && loadValid;

    ensureParentDirectory(options.output);
    std::ofstream out(options.output);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"suite\": \"perf-bench\",\n";
    out << "  \"preset\": \"" << genericString(presetPath) << "\",\n";
    out << "  \"notes\": \"" << options.benchNotes << "\",\n";
    out << "  \"sample_rate\": " << static_cast<int>(sampleRate) << ",\n";
    out << "  \"block_size\": " << blockSize << ",\n";
    out << "  \"set_parameters_per_block\": true,\n";
    out << "  \"bench_seconds\": " << benchSeconds << ",\n";
    out << "  \"warmup_seconds\": 1.5,\n";
    out << "  \"reps\": " << reps << ",\n";
    out << "  \"active_voices\": " << last.activeVoices << ",\n";
    out << "  \"peak\": " << last.peak << ",\n";
    out << "  \"invalid_samples\": " << totalInvalid << ",\n";
    out << "  \"rep_wall_ms\": [";
    for (std::size_t i = 0; i < repResults.size(); ++i)
        out << (i == 0 ? "" : ", ") << repResults[i].wallMs;
    out << "],\n";
    out << "  \"best_wall_ms\": " << bestWallMs << ",\n";
    out << "  \"median_wall_ms\": " << medianWallMs << ",\n";
    out << "  \"best_realtime_multiple\": " << bestRealtime << ",\n";
    out << "  \"median_realtime_multiple\": " << medianRealtime << ",\n";
    out << "  \"passed\": " << boolString(passed) << "\n";
    out << "}\n";

    std::cout << "bench preset=" << options.benchPreset
              << " voices=" << last.activeVoices
              << " block=" << blockSize
              << " audio_s=" << audioSeconds
              << " best_ms=" << bestWallMs
              << " median_ms=" << medianWallMs
              << " best_rt=" << bestRealtime << "x"
              << " median_rt=" << medianRealtime << "x"
              << (passed ? "" : " INVALID") << "\n";

    return passed ? 0 : 1;
}
} // namespace

int main(int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const auto options = parseOptions(argc, argv);

    if (!options.argumentError.empty())
    {
        std::cerr << options.argumentError << "\n";
        if (options.randomizeTest)
            writeFailureReport(options.output, "randomize-render", options.argumentError);
        return 2;
    }

    if (!options.suite.empty())
    {
        if (options.suite == "core")
            return writeCoreSuite(options);
        if (options.suite == "patch-recreation")
            return writePatchRecreationSuite(options);

        std::cerr << "Unknown suite: " << options.suite << "\n";
        return 2;
    }

    if (options.listParameters)
    {
        writeParameterReport(options.output);

        const auto errors = synth::validateParameterSpecs();
        if (!errors.empty())
        {
            std::cerr << "Parameter registry validation failed.\n";
            return 1;
        }

        std::cout << "Parameter report wrote " << synth::getParameterSpecs().size() << " parameters.\n";
        return 0;
    }

    if (options.validatePresets)
    {
        const auto exitCode = writePresetReport(options.output, options.presetPath);
        std::cout << "Preset validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.voiceTest)
    {
        const auto exitCode = writeVoiceReport(options.output);
        std::cout << "Voice core validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.oscTest)
    {
        const auto exitCode = writeOscillatorReport(options.output, options.notes);
        std::cout << "Oscillator validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.filterTest)
    {
        const auto exitCode = writeFilterReport(options.output);
        std::cout << "Filter validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.modulationTest)
    {
        const auto exitCode = writeModulationReport(options);
        std::cout << "Modulation validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.modulationRouteRenderTest)
    {
        const auto exitCode = writeModulationRouteRenderReport(options);
        std::cout << "Modulation route render validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.offlineRealtimeCompareTest)
    {
        const auto exitCode = writeOfflineRealtimeCompareReport(options);
        std::cout << "Offline/realtime compare validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.randomizeTest)
    {
        const auto exitCode = writeRandomizeReport(options);
        std::cout << "Randomize render validation " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (options.bench)
        return runPerfBench(options);

    if (options.presetRender)
    {
        const auto exitCode = renderPreset(options);
        std::cout << "Preset render " << (exitCode == 0 ? "passed." : "failed.") << "\n";
        return exitCode;
    }

    if (!options.smoke)
    {
        std::cerr << "No render mode selected. Use --help for modes.\n";
        return 2;
    }

    synth::SynthEngine engine;
    engine.prepare(48000.0, 256);

    std::vector<float> left(4800);
    std::vector<float> right(4800);
    const auto stats = engine.process(left.data(), right.data(), static_cast<int>(left.size()));

    writeSmokeReport(options.output, stats);

    if (stats.invalidSamples != 0)
    {
        std::cerr << "Smoke render failed: invalid samples detected.\n";
        return 1;
    }

    std::cout << "Smoke render passed: " << stats.samplesRendered << " samples.\n";
    return 0;
}
