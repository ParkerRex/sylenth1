#!/usr/bin/env python3
"""Repo-local C++ fitness checks for audio realtime safety and performance."""

from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Rule:
    rule_id: str
    pattern: re.Pattern[str]
    message: str


@dataclass(frozen=True)
class Violation:
    path: Path
    line_number: int
    rule_id: str
    message: str
    line: str


PRODUCTION_RULES = (
    Rule("concrete-types/no-std-any", re.compile(r"\bstd::any\b"), "use a concrete domain type instead of std::any"),
    Rule("concrete-types/no-dynamic-cast", re.compile(r"\bdynamic_cast\s*<"), "prefer explicit state/type models over RTTI"),
    Rule("concrete-types/no-unreviewed-void-pointer", re.compile(r"\b(?:const\s+)?void\s*\*"), "void pointers need a boundary allowlist"),
    Rule("concrete-types/no-unreviewed-reinterpret-cast", re.compile(r"\breinterpret_cast\s*<"), "reinterpret_cast needs a boundary allowlist"),
    Rule("ownership/no-raw-new", re.compile(r"\bnew\s+[A-Za-z_]"), "use RAII ownership or a reviewed JUCE factory boundary"),
    Rule("ownership/no-raw-delete", re.compile(r"\bdelete\b"), "use RAII ownership instead of raw delete"),
    Rule("errors/no-throw-in-production", re.compile(r"\bthrow\b"), "return explicit error state instead of throwing across plugin boundaries"),
    Rule("errors/no-catch-all", re.compile(r"catch\s*\(\s*\.\.\.\s*\)"), "catch explicit exception types at boundaries"),
)

REALTIME_RULES = (
    Rule("realtime/no-filesystem", re.compile(r"\bstd::filesystem::|\bjuce::File\b"), "no filesystem work on realtime paths"),
    Rule("realtime/no-juce-string", re.compile(r"\bjuce::String\b"), "avoid JUCE string work on realtime paths"),
    Rule("realtime/no-heap-allocation", re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\(|\bnew\s+[A-Za-z_]|\bdelete\b"), "no heap allocation on realtime paths"),
    Rule("realtime/no-container-mutation", re.compile(r"\.\s*(?:assign|resize|reserve|push_back|emplace_back)\s*\("), "preallocate container storage outside realtime paths"),
    Rule("realtime/no-locks", re.compile(r"\bstd::mutex\b|\bCriticalSection\b|\bScopedLock\b|\block_guard\b|\bunique_lock\b"), "no locks on realtime paths"),
    Rule("realtime/no-sync-logging", re.compile(r"\bDBG\s*\(|\bLogger::|\bstd::cout\b|\bstd::cerr\b|\bstd::clog\b"), "no synchronous logging on realtime paths"),
    Rule("realtime/no-threading", re.compile(r"\bstd::thread\b|\bstd::async\b"), "no thread creation/async work on realtime paths"),
    Rule("realtime/no-exceptions", re.compile(r"\bthrow\b|catch\s*\("), "no exceptions on realtime paths"),
)

AUDIO_PERFORMANCE_RULES = (
    Rule(
        "audio-perf/no-hot-pow-exp-log",
        re.compile(r"\bstd::(?:pow|exp|log|log10)\s*\("),
        "move pow/exp/log work out of hot audio paths or replace it with cached coefficients/approximations",
    ),
    Rule(
        "audio-perf/no-hot-tanh",
        re.compile(r"\bstd::tanh\s*\("),
        "avoid std::tanh in hot audio paths; use a bounded fast shaper or precomputed transfer where practical",
    ),
    Rule(
        "audio-perf/no-hot-sqrt",
        re.compile(r"\bstd::sqrt\s*\("),
        "cache normalization factors outside hot audio paths",
    ),
    Rule(
        "audio-perf/no-hot-rounding",
        re.compile(r"\bstd::(?:round|ceil|floor|fmod)\s*\("),
        "keep rounding/wrapping math out of hot audio paths unless the lookup or phase model requires it",
    ),
    Rule(
        "audio-perf/no-hot-setup",
        re.compile(r"\.\s*(?:prepare|setSize|setSampleRate|setCurrentAndTargetValue|reset)\s*\("),
        "prepare/reset resources outside hot audio paths",
    ),
    Rule(
        "audio-perf/no-hot-parameter-tree",
        re.compile(r"\b(?:getRawParameterValue|getParameter|copyState|replaceState)\s*\(|\bjuce::ValueTree\b"),
        "snapshot host state before hot audio rendering; do not query APVTS/ValueTree from hot paths",
    ),
    Rule(
        "audio-perf/no-hot-type-erasure",
        re.compile(r"\bstd::function\b|\bstd::bind\s*\("),
        "avoid type-erased call wrappers on hot audio paths",
    ),
)

HOT_AUDIO_FUNCTIONS = {
    "src/dsp/Arpeggiator.cpp": (
        "ArpGeneratedEvent Arpeggiator::processSample(",
        "int Arpeggiator::stepDurationSamples(",
    ),
    "src/dsp/Envelope.cpp": (
        "float Envelope::process()",
    ),
    "src/dsp/Filter.cpp": (
        "float softClip(",
        "float Filter::process(",
        "float Filter::cutoffSemitonesToHz(",
        "float Filter::processCore(",
        "float Filter::processPrepared(float input, float midiNote, const FilterParameters& parameters,",
        "void Filter::prepareBlock(const FilterParameters& filter)",
        "float Filter::saturateStage(",
        "void Filter::processStages(",
    ),
    "src/dsp/Lfo.cpp": (
        "float Lfo::process()",
        "float Lfo::valueForPhase(",
        "void Lfo::setNormalizedPhase(",
        "void Lfo::setPhaseDegrees(",
    ),
    "src/dsp/OscillatorStack.cpp": (
        "float OscillatorStack::renderSample(float midiNote, const SynthParameters& parameters,",
        "float OscillatorStack::renderSample(float midiNote, const OscillatorParameters& osc,",
        "OscillatorStack::StereoSample OscillatorStack::renderNative(",
    ),
    "src/dsp/Ramp.cpp": (
        "float Ramp::process(",
    ),
    "src/dsp/SynthEngine.cpp": (
        "RenderStats SynthEngine::process(",
        "void SynthEngine::advanceIdleModulators(",
    ),
    "src/dsp/fx/FxChain.cpp": (
        "float saturate(",
        "float foldDistort(",
        "float dbToGain(",
        "FxStereoFrame FxChain::process(",
        "FxStereoFrame FxChain::processSaturation(",
        "FxStereoFrame FxChain::processPhaser(",
        "FxStereoFrame FxChain::processChorus(",
        "FxStereoFrame FxChain::processEq(",
        "FxStereoFrame FxChain::processDelay(",
        "FxStereoFrame FxChain::processReverb(",
        "FxStereoFrame FxChain::processCompressor(",
        "FxStereoFrame FxChain::processPhaser(FxStereoFrame input, const FxParameters& parameters)",
        "FxStereoFrame FxChain::processChorus(FxStereoFrame input, const FxParameters& parameters)",
        "FxStereoFrame FxChain::processEq(FxStereoFrame input, const FxParameters& parameters)",
        "FxStereoFrame FxChain::processReverb(FxStereoFrame input, const FxParameters& parameters)",
        "void FxChain::updateCompressorCoefficients(",
        "FxStereoFrame FxChain::ReconstructionFilter::process(",
        "void FxChain::ReconstructionFilter::reset(",
        "float FxChain::DelayBuffer::readInterpolated(",
        "float FxChain::DelayBuffer::read(",
        "void FxChain::DelayBuffer::write(",
        "void FxChain::DelayBuffer::reset(",
        "float FxChain::Biquad::process(",
        "void FxChain::Biquad::setShelf(",
    ),
    "src/dsp/SynthParameters.h": (
        "inline float decibelsToGain(",
        "inline float gainToDecibels(",
        "inline float midiNoteToHz(",
        "inline float semitonesToHz(",
    ),
    "src/voice/Voice.cpp": (
        "void Voice::process(",
        "StereoFrame Voice::renderSample(",
        "StereoFrame Voice::renderNative(",
        "void Voice::syncModulatorConfig(",
        "float Voice::processGlide(",
        "float Voice::processVelocityGlide(",
        "Voice::ModulationSums Voice::evaluateTransMod(",
    ),
    "src/voice/VoiceAllocator.cpp": (
        "void VoiceAllocator::process(",
        "void VoiceAllocator::advanceIdleModulators(",
        "StereoFrame VoiceAllocator::renderSample(",
        "void VoiceAllocator::renderBlock(",
        "void VoiceAllocator::syncMonoLfoConfig(",
        "void VoiceAllocator::syncSecondMonoLfoConfig(",
    ),
}


def tracked_cpp_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "src/**/*.cpp", "src/**/*.h", "tests/**/*.cpp", "tests/**/*.h"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return [ROOT / line for line in result.stdout.splitlines() if line]


def is_production_file(path: Path) -> bool:
    relative = path.relative_to(ROOT)
    return relative.parts[0] == "src"


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0]


def allowed_production_violation(relative_path: str, rule_id: str, code: str) -> bool:
    if rule_id == "concrete-types/no-unreviewed-void-pointer":
        return "setStateInformation(const void*" in code or (
            relative_path == "src/presets/PresetManager.cpp"
            and "dladdr(reinterpret_cast<const void*>(&bundleFactoryPresetDirectory)" in code
        )

    if rule_id == "concrete-types/no-unreviewed-reinterpret-cast":
        return relative_path == "src/presets/PresetManager.cpp" and "dladdr(reinterpret_cast<const void*>(&bundleFactoryPresetDirectory)" in code

    if rule_id == "ownership/no-raw-new":
        return code.strip() in {
            "return new SynthAudioProcessorEditor(*this);",
            "return new SynthAudioProcessor();",
        }

    return False


def allowed_realtime_violation(relative_path: str, rule_id: str, code: str) -> bool:
    return (
        relative_path == "src/dsp/fx/FxChain.cpp"
        and rule_id == "realtime/no-container-mutation"
        and code.strip() == "samples.assign(static_cast<std::size_t>(std::max(2, sampleCount)), 0.0f);"
    )


def allowed_audio_performance_violation(relative_path: str, rule_id: str, code: str) -> bool:
    if relative_path == "src/dsp/Lfo.cpp" and rule_id == "audio-perf/no-hot-rounding":
        return code.strip() in {
            "phase -= std::floor(phase);",
            "phaseValue -= std::floor(phaseValue);",
            # Shared native LFO phase plus per-voice rate offset needs unit wrapping.
            "phase = std::isfinite(normalizedPhase) ? normalizedPhase - std::floor(normalizedPhase) : 0.0f;",
        }

    if relative_path == "src/voice/Voice.cpp" and rule_id == "audio-perf/no-hot-rounding":
        # Finite clamped rates accumulate a bounded per-voice phase offset.
        return code.strip() in {
            "nativeLfo1PhaseOffset -= std::floor(nativeLfo1PhaseOffset);",
            "nativeLfo2PhaseOffset -= std::floor(nativeLfo2PhaseOffset);",
        }

    if relative_path == "src/dsp/OscillatorStack.cpp" and rule_id == "audio-perf/no-hot-rounding":
        return "phase -= std::floor(phase);" in code

    if relative_path == "src/dsp/fx/FxChain.cpp" and rule_id == "audio-perf/no-hot-rounding":
        return code.strip() in {
            "phaserPhase -= std::floor(phaserPhase);",
            "chorusPhase -= std::floor(chorusPhase);",
            # A fractional delay tap interpolates its two adjacent stored samples.
            "const auto floorDelay = static_cast<int>(std::floor(delaySamples));",
        }

    if relative_path == "src/dsp/fx/FxChain.cpp" and rule_id == "audio-perf/no-hot-setup":
        # DelayBuffer reset invalidates two counters, Biquad reset clears two
        # floats, and reconstruction resets four Biquads. No storage clearing,
        # allocation, resource construction or locks occur in these methods.
        return code.strip() in {
            "shelf.reset();", "filter.reset();", "delayLeft.reset();", "delayRight.reset();",
            "diffuser.reset();", "chorusLeft.reset();", "chorusRight.reset();",
            "chorusSecondLeft.reset();", "chorusSecondRight.reset();",
        }

    if relative_path == "src/dsp/fx/FxChain.cpp" and rule_id == "audio-perf/no-hot-pow-exp-log":
        # These coefficient updates are guarded by changed cutoff/attack/release
        # values from the block snapshot. They never depend on sample input.
        return code.strip() in {
            "delayLowCoefficient = 1.0f - std::exp(-twoPi * lowCut / static_cast<float>(sampleRate));",
            "delayHighCoefficient = 1.0f - std::exp(-twoPi * highCut / static_cast<float>(sampleRate));",
            "compressorAttackCoefficient = 1.0f - std::exp(-1.0f / (attack * 0.001f * static_cast<float>(sampleRate)));",
            "compressorReleaseCoefficient = 1.0f - std::exp(-1.0f / (release * 0.001f * static_cast<float>(sampleRate)));",
            # Biquad shelves are rebuilt only inside settings != eqSettings.
            "const auto amplitude = std::pow(10.0f, gainDb / 40.0f);",
        }

    if relative_path == "src/dsp/fx/FxChain.cpp" and rule_id == "audio-perf/no-hot-sqrt":
        # The same guarded shelf rebuild computes its fixed coefficient vector.
        return code.strip() == "const auto beta = std::sin(omega) * std::sqrt(2.0f * amplitude);"

    return False


def scan_lines(path: Path, rules: tuple[Rule, ...], realtime: bool) -> list[Violation]:
    relative_path = path.relative_to(ROOT).as_posix()
    violations: list[Violation] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        code = strip_line_comment(line)
        if not code.strip():
            continue

        for rule in rules:
            if not rule.pattern.search(code):
                continue

            allowed = (
                allowed_realtime_violation(relative_path, rule.rule_id, code)
                if realtime
                else allowed_production_violation(relative_path, rule.rule_id, code)
            )
            if allowed:
                continue

            violations.append(
                Violation(
                    path=path,
                    line_number=line_number,
                    rule_id=rule.rule_id,
                    message=rule.message,
                    line=line.rstrip(),
                )
            )

    return violations


def scan_lines_for_rules(path: Path, start_line: int, lines: list[str], rules: tuple[Rule, ...]) -> list[Violation]:
    relative_path = path.relative_to(ROOT).as_posix()
    violations: list[Violation] = []
    for offset, line in enumerate(lines):
        code = strip_line_comment(line)
        if not code.strip():
            continue

        for rule in rules:
            if not rule.pattern.search(code):
                continue
            if allowed_audio_performance_violation(relative_path, rule.rule_id, code):
                continue
            violations.append(
                Violation(
                    path=path,
                    line_number=start_line + offset,
                    rule_id=rule.rule_id,
                    message=rule.message,
                    line=line.rstrip(),
                )
            )
    return violations


def extract_function_body(path: Path, signature: str) -> tuple[int, list[str]]:
    text = path.read_text(encoding="utf-8")
    signature_index = text.find(signature)
    if signature_index < 0:
        raise RuntimeError(f"missing realtime function signature: {path.relative_to(ROOT)}::{signature}")

    open_brace = text.find("{", signature_index)
    if open_brace < 0:
        raise RuntimeError(f"missing function body: {path.relative_to(ROOT)}::{signature}")

    line_offset = text[:open_brace].count("\n") + 1
    depth = 0
    end_index = open_brace
    for index in range(open_brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end_index = index + 1
                break
    else:
        raise RuntimeError(f"unterminated function body: {path.relative_to(ROOT)}::{signature}")

    return line_offset, text[open_brace:end_index].splitlines()


def scan_extracted_lines(path: Path, start_line: int, lines: list[str]) -> list[Violation]:
    relative_path = path.relative_to(ROOT).as_posix()
    violations: list[Violation] = []
    for offset, line in enumerate(lines):
        code = strip_line_comment(line)
        if not code.strip():
            continue

        for rule in REALTIME_RULES:
            if not rule.pattern.search(code):
                continue
            if allowed_realtime_violation(relative_path, rule.rule_id, code):
                continue
            violations.append(
                Violation(
                    path=path,
                    line_number=start_line + offset,
                    rule_id=rule.rule_id,
                    message=rule.message,
                    line=line.rstrip(),
                )
            )
    return violations


def scan_hot_audio_functions(files: list[Path]) -> list[Violation]:
    tracked = {path.relative_to(ROOT).as_posix(): path for path in files}
    violations: list[Violation] = []
    for relative_path, signatures in HOT_AUDIO_FUNCTIONS.items():
        path = tracked.get(relative_path)
        if path is None:
            raise RuntimeError(f"missing hot audio file from git tracking: {relative_path}")

        for signature in signatures:
            start_line, lines = extract_function_body(path, signature)
            violations.extend(scan_lines_for_rules(path, start_line, lines, AUDIO_PERFORMANCE_RULES))

    return violations


def realtime_files(files: list[Path]) -> list[Path]:
    roots = {
        ROOT / "src/dsp",
        ROOT / "src/voice",
    }
    selected: list[Path] = []
    for path in files:
        if path.suffix not in {".cpp", ".h", ".hpp"}:
            continue
        if any(root in path.parents for root in roots):
            selected.append(path)
    return selected


def main() -> int:
    files = tracked_cpp_files()
    violations: list[Violation] = []

    for path in files:
        if is_production_file(path):
            violations.extend(scan_lines(path, PRODUCTION_RULES, realtime=False))

    for path in realtime_files(files):
        violations.extend(scan_lines(path, REALTIME_RULES, realtime=True))

    violations.extend(scan_hot_audio_functions(files))

    processor_path = ROOT / "src/plugin/PluginProcessor.cpp"
    for signature in (
        "void SynthAudioProcessor::processBlock(",
        "void SynthAudioProcessor::handleMidiMessage(",
        "void SynthAudioProcessor::handleMappedController(",
        "synth::RenderStats SynthAudioProcessor::renderSegment(",
        "void SynthAudioProcessor::drainUiMidiMessages(",
    ):
        start_line, lines = extract_function_body(processor_path, signature)
        violations.extend(scan_extracted_lines(processor_path, start_line, lines))

    if violations:
        print("C++ fitness gate failed:")
        for violation in violations:
            relative = violation.path.relative_to(ROOT)
            print(f"{relative}:{violation.line_number}: {violation.rule_id}: {violation.message}")
            print(f"  {violation.line.strip()}")
        return 1

    print("C++ fitness gate passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
