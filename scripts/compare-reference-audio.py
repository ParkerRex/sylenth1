#!/usr/bin/env python3
"""Compare original-plugin PCM WAV fixtures without alignment or gain changes."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import wave
from pathlib import Path


def read_pcm(path: Path) -> tuple[int, int, list[float]]:
    with wave.open(str(path), 'rb') as reader:
        if reader.getcomptype() != 'NONE' or reader.getsampwidth() not in (1, 2, 3, 4):
            raise ValueError('Reference comparison requires uncompressed integer PCM WAV')
        rate, channels, width = reader.getframerate(), reader.getnchannels(), reader.getsampwidth()
        frames = reader.getnframes()
        raw = reader.readframes(frames)
    if not frames or len(raw) != frames * channels * width:
        raise ValueError('Empty or truncated WAV fixture')
    if width == 1:
        samples = [(value - 128) / 128 for value in raw]
    elif width in (2, 4):
        code = 'h' if width == 2 else 'i'
        scale = 2 ** (8 * width - 1)
        samples = [sample[0] / scale for sample in struct.iter_unpack('<' + code, raw)]
    else:
        samples = [int.from_bytes(raw[index:index + 3], 'little', signed=True) / 8388608
                   for index in range(0, len(raw), 3)]
    return rate, channels, samples


def compare_audio(reference: Path, candidate: Path, maximum: float, rms_limit: float, allow_silence: bool = False) -> dict[str, object]:
    if not math.isfinite(maximum) or not math.isfinite(rms_limit) or maximum < 0 or rms_limit < 0:
        raise ValueError('Audio difference limits must be finite and nonnegative')
    if aliases(reference, candidate):
        raise ValueError('Reference and candidate must be independently captured files, not aliases')
    rate, channels, original = read_pcm(reference)
    candidate_rate, candidate_channels, rebuilt = read_pcm(candidate)
    if rate != candidate_rate or channels != candidate_channels or len(original) != len(rebuilt):
        raise ValueError('Sample-rate, channel-count or frame-count mismatch; resampling, trimming and alignment are forbidden')
    if not allow_silence and (not any(original) or not any(rebuilt)):
        raise ValueError('Silent musical capture requires explicit silence approval')
    channel_metrics = []
    overall_maximum = overall_square_sum = 0.0
    for channel in range(channels):
        count = len(original) // channels
        square_sum = max_difference = reference_square_sum = candidate_square_sum = cross_sum = 0.0
        for index in range(channel, len(original), channels):
            first, second = original[index], rebuilt[index]
            difference = first - second
            square_sum += difference * difference
            max_difference = max(max_difference, abs(difference))
            reference_square_sum += first * first
            candidate_square_sum += second * second
            cross_sum += first * second
        overall_maximum = max(overall_maximum, max_difference)
        overall_square_sum += square_sum
        denominator = math.sqrt(reference_square_sum * candidate_square_sum)
        channel_metrics.append({'channel': channel, 'max_absolute_difference': max_difference,
                                'rms_difference': math.sqrt(square_sum / count),
                                'reference_rms': math.sqrt(reference_square_sum / count),
                                'candidate_rms': math.sqrt(candidate_square_sum / count),
                                'normalized_cross_correlation': cross_sum / denominator if denominator else None})
    rms_difference = math.sqrt(overall_square_sum / len(original))
    return {'sample_rate': rate, 'channels': channels, 'frames': len(original) // channels,
            'max_absolute_difference': overall_maximum, 'rms_difference': rms_difference,
            'channel_metrics': channel_metrics, 'limits': {'max_absolute_difference': maximum, 'rms_difference': rms_limit},
            'passed': overall_maximum <= maximum and rms_difference <= rms_limit}


def aliases(first: Path, second: Path) -> bool:
    if first.resolve() == second.resolve():
        return True
    return first.exists() and second.exists() and first.samefile(second)


def protect_output(output: Path, manifest_path: Path) -> None:
    if aliases(output, manifest_path):
        raise ValueError('Report output aliases the manifest')
    manifest = json.loads(manifest_path.read_text())
    for group in ('images', 'audio'):
        for case in manifest.get(group, []):
            if not isinstance(case, dict):
                continue
            for field in ('reference', 'candidate'):
                if isinstance(case.get(field), str) and case[field] and aliases(output, manifest_path.parent / case[field]):
                    raise ValueError('Report output aliases a capture fixture')


def compare_manifest(manifest_path: Path) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('schema_version') != 1 or not isinstance(manifest.get('audio'), list) or not manifest['audio']:
        raise ValueError('Manifest requires schema_version 1 and at least one audio case')
    if not all(isinstance(case, dict) for case in manifest['audio']):
        raise ValueError('Each reference case must be an object')
    results = []
    for index, case in enumerate(manifest['audio']):
        result: dict[str, object] = {'id': case.get('id', f'audio-{index}'), 'passed': False}
        try:
            for field in ('reference', 'candidate'):
                if not isinstance(case.get(field), str) or not case[field]:
                    raise ValueError(f'Missing {field}: original PCM recordings are required')
            source = case.get('source', {})
            if source.get('product') != 'Sylenth1' or source.get('capture_origin') != 'original_plugin' or not source.get('captured_by'):
                raise ValueError('Original Sylenth1 recording provenance is required')
            reference = manifest_path.parent / case['reference']
            candidate = manifest_path.parent / case['candidate']
            digest = hashlib.sha256(reference.read_bytes()).hexdigest()
            if digest != source.get('sha256'):
                raise ValueError('Original reference SHA-256 is missing or mismatched')
            settings = case.get('capture_settings', {})
            required_settings = ('sample_rate', 'block_size', 'tempo_bpm', 'midi_fixture', 'preset_state', 'plugin_version', 'host_version')
            if any(settings.get(field) in (None, '') for field in required_settings):
                raise ValueError('Capture settings must record sample rate, buffer, tempo, MIDI, patch, original plugin and host versions')
            limits = case.get('limits', {})
            if not limits.get('approved_by') or not limits.get('reason'):
                raise ValueError('Audio limits require explicit approval and a reason; no default fidelity tolerance is inferred')
            silence = case.get('silence_approval', {})
            allow_silence = bool(silence.get('approved_by') and silence.get('reason'))
            result.update(compare_audio(reference, candidate, float(limits['max_absolute_difference']), float(limits['rms_difference']), allow_silence))
            if settings['sample_rate'] != result['sample_rate']:
                raise ValueError('Recorded capture sample rate does not match WAV metadata')
            result.update({'source': source, 'capture_settings': settings, 'approved_limits': limits,
                           'reference_sha256': digest, 'candidate_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest()})
        except (OSError, ValueError, KeyError, TypeError, wave.Error) as error:
            result['passed'] = False
            result['error'] = str(error)
        results.append(result)
    return {'schema_version': 1, 'suite': 'original-reference-audio', 'manifest': str(manifest_path),
            'policy': 'Fixed sample rate, channels, frames and amplitude; no alignment, gain matching, resampling or inferred limits',
            'cases': results, 'passed': all(case['passed'] for case in results)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    try:
        protect_output(arguments.output, arguments.manifest)
    except (ValueError, OSError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    try:
        report = compare_manifest(arguments.manifest)
    except (OSError, ValueError, TypeError) as error:
        report = {'schema_version': 1, 'suite': 'original-reference-audio', 'passed': False, 'error': str(error)}
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Audio reference comparison {"passed" if report["passed"] else "failed"}: {arguments.output}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
