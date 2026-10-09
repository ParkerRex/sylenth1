#!/usr/bin/env python3
"""Positive and consequential negative controls for external-reference tools."""
from __future__ import annotations

import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_tool(filename: str):
    specification = importlib.util.spec_from_file_location(filename.replace('-', '_'), ROOT / 'scripts' / filename)
    if specification is None or specification.loader is None:
        raise RuntimeError('Unable to load reference comparison tool')
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


IMAGES = load_tool('compare-reference-images.py')
AUDIO = load_tool('compare-reference-audio.py')


def write_wav(path: Path, samples: list[int], channels: int = 2, rate: int = 48000) -> None:
    with wave.open(str(path), 'wb') as writer:
        writer.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
        writer.writeframes(struct.pack('<' + 'h' * len(samples), *samples))


class ReferenceComparisonTest(unittest.TestCase):
    def test_image_exact_and_alpha_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            pixels = bytes((10, 20, 30, 255, 30, 20, 10, 255))
            reference, candidate = folder / 'reference.png', folder / 'candidate.png'
            IMAGES.write_rgba(reference, 2, 1, pixels)
            IMAGES.write_rgba(candidate, 2, 1, pixels)
            self.assertTrue(IMAGES.compare_images(reference, candidate, [], folder / 'same.png')['passed'])
            IMAGES.write_rgba(candidate, 2, 1, pixels[:-1] + b'\xfe')
            result = IMAGES.compare_images(reference, candidate, [], folder / 'alpha.png')
            self.assertFalse(result['passed'])
            self.assertEqual(result['changed_pixels'], 1)
            self.assertEqual(result['max_channel_difference'], 1)
            self.assertTrue((folder / 'alpha.png').exists())

    def test_only_explicit_branding_rectangle_is_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            reference, candidate = folder / 'reference.png', folder / 'candidate.png'
            IMAGES.write_rgba(reference, 2, 1, bytes((0, 0, 0, 255)) * 2)
            IMAGES.write_rgba(candidate, 2, 1, bytes((255, 0, 0, 255, 0, 0, 0, 255)))
            mask = {'x': 0, 'y': 0, 'width': 1, 'height': 1, 'purpose': 'branding',
                    'approved_by': 'synthetic-test-only', 'reason': 'Synthetic mask boundary test'}
            result = IMAGES.compare_images(reference, candidate, [mask], folder / 'masked.png')
            self.assertTrue(result['passed'])
            self.assertEqual(result['masked_pixels'], 1)
            IMAGES.write_rgba(candidate, 2, 1, bytes((255, 0, 0, 255, 1, 0, 0, 255)))
            self.assertFalse(IMAGES.compare_images(reference, candidate, [mask], folder / 'outside.png')['passed'])
            with self.assertRaises(ValueError):
                IMAGES.compare_images(reference, candidate, [{**mask, 'purpose': 'controls'}], folder / 'invalid.png')
            with self.assertRaises(ValueError):
                IMAGES.compare_images(reference, candidate, [{**mask, 'width': 2}], folder / 'entire.png')

    def test_resolution_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            IMAGES.write_rgba(folder / 'a.png', 2, 1, bytes((0, 0, 0, 255)) * 2)
            IMAGES.write_rgba(folder / 'b.png', 1, 2, bytes((0, 0, 0, 255)) * 2)
            with self.assertRaises(ValueError):
                IMAGES.compare_images(folder / 'a.png', folder / 'b.png', [], folder / 'diff.png')

    def test_audio_exact_changed_channel_and_timing(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            samples = [1000, -1000, 2000, -2000, 0, 0, -1000, 1000]
            write_wav(folder / 'a.wav', samples)
            write_wav(folder / 'b.wav', samples)
            self.assertTrue(AUDIO.compare_audio(folder / 'a.wav', folder / 'b.wav', 0, 0)['passed'])
            changed = samples.copy()
            changed[1] += 1
            write_wav(folder / 'b.wav', changed)
            result = AUDIO.compare_audio(folder / 'a.wav', folder / 'b.wav', 0, 0)
            self.assertFalse(result['passed'])
            self.assertEqual(result['channel_metrics'][0]['max_absolute_difference'], 0)
            self.assertGreater(result['channel_metrics'][1]['max_absolute_difference'], 0)
            write_wav(folder / 'b.wav', samples[2:] + samples[:2])
            self.assertFalse(AUDIO.compare_audio(folder / 'a.wav', folder / 'b.wav', 0, 0)['passed'])
            write_wav(folder / 'b.wav', samples, rate=44100)
            with self.assertRaises(ValueError):
                AUDIO.compare_audio(folder / 'a.wav', folder / 'b.wav', 0, 0)
            write_wav(folder / 'b.wav', samples[:-2])
            with self.assertRaises(ValueError):
                AUDIO.compare_audio(folder / 'a.wav', folder / 'b.wav', 0, 0)

    def test_reference_aliases_and_capture_overwrites_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            image = folder / 'reference.png'
            IMAGES.write_rgba(image, 1, 1, bytes((10, 20, 30, 255)))
            identical = folder / 'candidate.png'
            IMAGES.write_rgba(identical, 1, 1, bytes((10, 20, 30, 255)))
            original_bytes = image.read_bytes()
            with self.assertRaises(ValueError):
                IMAGES.compare_images(image, image, [], folder / 'difference.png')
            with self.assertRaises(ValueError):
                IMAGES.compare_images(image, identical, [], image)
            self.assertEqual(image.read_bytes(), original_bytes)
            linked = folder / 'linked.png'
            os.link(image, linked)
            with self.assertRaises(ValueError):
                IMAGES.compare_images(image, linked, [], folder / 'difference.png')
            write_wav(folder / 'reference.wav', [100, -100, 200, -200])
            alias = folder / 'alias.wav'
            alias.symlink_to(folder / 'reference.wav')
            with self.assertRaises(ValueError):
                AUDIO.compare_audio(folder / 'reference.wav', alias, 0, 0)
            manifest = folder / 'manifest.json'
            manifest.write_text(json.dumps({'images': [{'reference': 'reference.png', 'candidate': 'candidate.png'}]}))
            with self.assertRaises(ValueError):
                IMAGES.protect_output(image, manifest)
            with self.assertRaises(ValueError):
                AUDIO.protect_output(manifest, manifest)

    def test_silent_audio_requires_explicit_silence_case(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            write_wav(folder / 'reference.wav', [0, 0, 0, 0])
            write_wav(folder / 'candidate.wav', [0, 0, 0, 0])
            with self.assertRaises(ValueError):
                AUDIO.compare_audio(folder / 'reference.wav', folder / 'candidate.wav', 0, 0)
            self.assertTrue(AUDIO.compare_audio(folder / 'reference.wav', folder / 'candidate.wav', 0, 0, allow_silence=True)['passed'])

    def test_missing_reference_manifest_fails_both_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            manifest = ROOT / 'tests/references/sylenth-reference-manifest.json'
            commands = [
                [sys.executable, str(ROOT / 'scripts/compare-reference-images.py'), '--manifest', str(manifest), '--output-dir', str(folder / 'images')],
                [sys.executable, str(ROOT / 'scripts/compare-reference-audio.py'), '--manifest', str(manifest), '--output', str(folder / 'audio.json')],
            ]
            for command in commands:
                result = subprocess.run(command, capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 1, result.stderr)
            for report in (folder / 'images/summary.json', folder / 'audio.json'):
                data = json.loads(report.read_text())
                self.assertFalse(data['passed'])
                self.assertTrue(all('Missing reference' in case['error'] for case in data['cases']))


if __name__ == '__main__':
    unittest.main()
