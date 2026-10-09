#!/usr/bin/env python3
"""Compare original-plugin PNG captures with same-size native RGBA snapshots."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
import zlib
from pathlib import Path


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def write_rgba(path: Path, width: int, height: int, pixels: bytes) -> None:
    raw = b''.join(b'\0' + pixels[row * width * 4:(row + 1) * width * 4] for row in range(height))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + png_chunk(b'IDAT', zlib.compress(raw)) + png_chunk(b'IEND', b''))


def read_rgba(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: expected lossless PNG')
    offset = 8
    compressed = bytearray()
    width = height = 0
    finished = False
    while offset + 12 <= len(data):
        size = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        crc = data[offset + 8 + size:offset + 12 + size]
        if len(crc) != 4 or struct.unpack('>I', crc)[0] != zlib.crc32(kind + payload):
            raise ValueError(f'{path}: invalid PNG chunk')
        if kind == b'IHDR':
            width, height, bits, colour, compression, filtering, interlace = struct.unpack('>IIBBBBB', payload)
            if bits != 8 or colour != 6 or compression or filtering or interlace:
                raise ValueError(f'{path}: required 8-bit RGBA, non-interlaced PNG; convert explicitly before comparison')
            if width <= 0 or height <= 0 or width * height > 16000000:
                raise ValueError(f'{path}: unsupported image dimensions')
        elif kind == b'IDAT':
            compressed.extend(payload)
        elif kind == b'IEND':
            finished = True
            break
        offset += 12 + size
    if not width or not finished:
        raise ValueError(f'{path}: incomplete PNG')
    stride = width * 4
    expected_bytes = (stride + 1) * height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected_bytes + 1)
    if len(raw) != expected_bytes or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
        raise ValueError(f'{path}: invalid PNG scanline length')
    pixels = bytearray()
    previous = bytearray(stride)
    for row_index in range(height):
        start = row_index * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ValueError(f'{path}: unsupported PNG filter')
        for index in range(stride):
            left = row[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            predictor = 0
            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            row[index] = (row[index] + predictor) & 255
        pixels.extend(row)
        previous = row
    return width, height, bytes(pixels)


def compare_images(reference: Path, candidate: Path, masks: list[dict[str, object]], difference_path: Path) -> dict[str, object]:
    if aliases(reference, candidate):
        raise ValueError('Reference and candidate must be independently captured files, not aliases')
    if aliases(difference_path, reference) or aliases(difference_path, candidate):
        raise ValueError('Difference output aliases an input capture')
    width, height, original = read_rgba(reference)
    candidate_width, candidate_height, rebuilt = read_rgba(candidate)
    if (width, height) != (candidate_width, candidate_height):
        raise ValueError('Resolution mismatch; resizing is forbidden')
    masked = bytearray(width * height)
    for mask in masks:
        if mask.get('purpose') != 'branding' or not mask.get('approved_by') or not mask.get('reason'):
            raise ValueError('Every mask requires branding purpose, explicit approver and reason')
        rectangle = [mask.get(field) for field in ('x', 'y', 'width', 'height')]
        if not all(type(value) is int for value in rectangle):
            raise ValueError('Mask coordinates must be integers')
        x, y, mask_width, mask_height = rectangle
        if x < 0 or y < 0 or mask_width <= 0 or mask_height <= 0 or x + mask_width > width or y + mask_height > height:
            raise ValueError('Mask rectangle exceeds image bounds')
        for row in range(y, y + mask_height):
            masked[row * width + x:row * width + x + mask_width] = b'\1' * mask_width
    compared = len(masked) - sum(masked)
    if compared == 0:
        raise ValueError('Branding masks exclude the entire image')
    difference_pixels = bytearray(len(original))
    changed = maximum = total = squared = 0
    for pixel in range(width * height):
        start = pixel * 4
        if masked[pixel]:
            difference_pixels[start:start + 4] = bytes((0, 80, 255, 255))
            continue
        channel_differences = [abs(original[start + channel] - rebuilt[start + channel]) for channel in range(4)]
        changed += any(channel_differences)
        maximum = max(maximum, max(channel_differences))
        total += sum(channel_differences)
        squared += sum(value * value for value in channel_differences)
        difference_pixels[start:start + 4] = bytes((channel_differences[0], channel_differences[1],
                                                   max(channel_differences[2], channel_differences[3]), 255))
    write_rgba(difference_path, width, height, difference_pixels)
    return {'width': width, 'height': height, 'compared_pixels': compared, 'masked_pixels': sum(masked),
            'changed_pixels': changed, 'changed_fraction': changed / compared, 'max_channel_difference': maximum,
            'mean_absolute_channel_difference': total / (compared * 4),
            'rms_channel_difference': math.sqrt(squared / (compared * 4)),
            'difference_image': str(difference_path), 'approved_branding_masks': masks, 'passed': changed == 0}


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


def compare_manifest(manifest_path: Path, output_dir: Path) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('schema_version') != 1 or not isinstance(manifest.get('images'), list) or not manifest['images']:
        raise ValueError('Manifest requires schema_version 1 and at least one image case')
    if not all(isinstance(case, dict) for case in manifest['images']):
        raise ValueError('Each reference case must be an object')
    cases = []
    for index, case in enumerate(manifest['images']):
        result: dict[str, object] = {'id': case.get('id', f'image-{index}'), 'passed': False}
        try:
            for field in ('reference', 'candidate'):
                if not isinstance(case.get(field), str) or not case[field]:
                    raise ValueError(f'Missing {field}: original lossless captures are required')
            reference = manifest_path.parent / case['reference']
            candidate = manifest_path.parent / case['candidate']
            source = case.get('source', {})
            if source.get('product') != 'Sylenth1' or source.get('capture_origin') != 'original_plugin' or not source.get('captured_by'):
                raise ValueError('Original Sylenth1 capture provenance is required')
            digest = hashlib.sha256(reference.read_bytes()).hexdigest()
            if source.get('sha256') != digest:
                raise ValueError('Original reference SHA-256 is missing or mismatched')
            difference_path = output_dir / f'image-{index}-difference.png'
            protect_output(difference_path, manifest_path)
            result.update(compare_images(reference, candidate, case.get('branding_masks', []), difference_path))
            result.update({'reference_sha256': digest, 'candidate_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest(), 'source': source})
        except (ValueError, OSError, KeyError, TypeError, zlib.error) as error:
            result['passed'] = False
            result['error'] = str(error)
        cases.append(result)
    return {'schema_version': 1, 'suite': 'original-reference-images', 'manifest': str(manifest_path),
            'policy': 'Exact RGBA pixels at identical resolution outside explicitly approved branding masks; no resizing or tolerance',
            'cases': cases, 'passed': all(case['passed'] for case in cases)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    arguments = parser.parse_args()
    try:
        protect_output(arguments.output_dir / 'summary.json', arguments.manifest)
    except (ValueError, OSError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    try:
        report = compare_manifest(arguments.manifest, arguments.output_dir)
    except (ValueError, OSError, TypeError) as error:
        report = {'schema_version': 1, 'suite': 'original-reference-images', 'passed': False, 'error': str(error)}
    output = arguments.output_dir / 'summary.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Image reference comparison {"passed" if report["passed"] else "failed"}: {output}')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
