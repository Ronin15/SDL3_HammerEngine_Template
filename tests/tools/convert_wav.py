#!/usr/bin/env python3
"""
Convert 24-bit WAV files to 16-bit for SDL3_mixer compatibility.
Creates clean WAV files without extra chunks.
"""
import struct
import sys
from pathlib import Path

def convert_24bit_to_16bit(input_path, output_path, target_rate=44100):
    """Convert 24-bit PCM WAV to 16-bit PCM WAV with optional resampling."""

    with open(input_path, 'rb') as f:
        # Read RIFF header
        riff = f.read(4)
        if riff != b'RIFF':
            raise ValueError(f"Not a RIFF file: {input_path}")

        file_size = struct.unpack('<I', f.read(4))[0]
        wave = f.read(4)
        if wave != b'WAVE':
            raise ValueError(f"Not a WAVE file: {input_path}")

        # Find fmt chunk
        while True:
            chunk_id = f.read(4)
            if not chunk_id:
                raise ValueError("fmt chunk not found")
            chunk_size = struct.unpack('<I', f.read(4))[0]

            if chunk_id == b'fmt ':
                fmt_data = f.read(chunk_size)
                audio_format = struct.unpack('<H', fmt_data[0:2])[0]
                channels = struct.unpack('<H', fmt_data[2:4])[0]
                sample_rate = struct.unpack('<I', fmt_data[4:8])[0]
                bits_per_sample = struct.unpack('<H', fmt_data[14:16])[0]

                if audio_format != 1:  # PCM
                    raise ValueError(f"Unsupported audio format: {audio_format}")
                if bits_per_sample != 24:
                    raise ValueError(f"Expected 24-bit, got {bits_per_sample}-bit")

                break
            else:
                f.seek(chunk_size, 1)  # Skip chunk

        # Find data chunk
        while True:
            chunk_id = f.read(4)
            if not chunk_id:
                raise ValueError("data chunk not found")
            chunk_size = struct.unpack('<I', f.read(4))[0]

            if chunk_id == b'data':
                # Read all audio data
                audio_data = f.read(chunk_size)
                break
            else:
                f.seek(chunk_size, 1)  # Skip chunk

    # Convert 24-bit samples to 16-bit
    num_samples = len(audio_data) // 3
    samples_16bit = []

    for i in range(num_samples):
        # Read 24-bit sample (little-endian)
        byte1 = audio_data[i * 3]
        byte2 = audio_data[i * 3 + 1]
        byte3 = audio_data[i * 3 + 2]

        # Convert to signed 24-bit integer
        sample_24 = byte1 | (byte2 << 8) | (byte3 << 16)
        if sample_24 & 0x800000:  # Sign extend if negative
            sample_24 |= 0xFF000000
            sample_24 = struct.unpack('i', struct.pack('I', sample_24))[0]

        # Convert to 16-bit by taking upper 16 bits
        sample_16 = sample_24 >> 8
        sample_16 = max(-32768, min(32767, sample_16))  # Clamp
        samples_16bit.append(sample_16)

    # Resample if needed
    original_rate = sample_rate
    if sample_rate != target_rate:
        ratio = sample_rate / target_rate
        num_frames = len(samples_16bit) // channels
        new_num_frames = int(num_frames / ratio)
        resampled = []

        for i in range(new_num_frames):
            src_pos = i * ratio
            src_idx = int(src_pos)
            frac = src_pos - src_idx

            for ch in range(channels):
                idx1 = src_idx * channels + ch
                idx2 = min(idx1 + channels, len(samples_16bit) - 1)

                # Linear interpolation
                sample = int(samples_16bit[idx1] * (1 - frac) + samples_16bit[idx2] * frac)
                resampled.append(sample)

        samples_16bit = resampled
        sample_rate = target_rate

    # Write 16-bit WAV file
    output_data = struct.pack('<' + 'h' * len(samples_16bit), *samples_16bit)

    # Calculate sizes
    data_size = len(output_data)
    fmt_chunk_size = 16
    file_size = 4 + 8 + fmt_chunk_size + 8 + data_size  # WAVE + fmt chunk + data chunk

    # New format parameters for 16-bit
    byte_rate = sample_rate * channels * 2  # 2 bytes per sample
    block_align = channels * 2

    with open(output_path, 'wb') as f:
        # RIFF header
        f.write(b'RIFF')
        f.write(struct.pack('<I', file_size))
        f.write(b'WAVE')

        # fmt chunk
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))  # fmt chunk size
        f.write(struct.pack('<H', 1))   # PCM format
        f.write(struct.pack('<H', channels))
        f.write(struct.pack('<I', sample_rate))
        f.write(struct.pack('<I', byte_rate))
        f.write(struct.pack('<H', block_align))
        f.write(struct.pack('<H', 16))  # 16 bits per sample

        # data chunk
        f.write(b'data')
        f.write(struct.pack('<I', data_size))
        f.write(output_data)

    resample_info = f" @ {original_rate}Hz → {target_rate}Hz" if original_rate != target_rate else f" @ {target_rate}Hz"
    print(f"✓ Converted {input_path.name}: {bits_per_sample}-bit → 16-bit, {channels}ch{resample_info}")
    return True

if __name__ == '__main__':
    sfx_dir = Path('res/sfx')

    for wav_file in sfx_dir.glob('*.wav'):
        if wav_file.stem.endswith('_backup'):
            continue

        # Check if it's 24-bit
        try:
            with open(wav_file, 'rb') as f:
                # Skip RIFF header
                f.seek(12)  # Skip "RIFF" + size + "WAVE"

                # Find fmt chunk
                while True:
                    chunk_id = f.read(4)
                    if not chunk_id:
                        break
                    chunk_size = struct.unpack('<I', f.read(4))[0]

                    if chunk_id == b'fmt ':
                        # Read format info
                        fmt_start = f.tell()
                        f.seek(fmt_start + 14)  # bits_per_sample offset in fmt
                        bits = struct.unpack('<H', f.read(2))[0]
                        break
                    else:
                        f.seek(chunk_size, 1)

                if bits == 24:
                    backup = wav_file.with_suffix('.wav.bak')
                    output = wav_file

                    # Backup original
                    wav_file.rename(backup)

                    # Convert
                    convert_24bit_to_16bit(backup, output)
                else:
                    print(f"⊘ Skipping {wav_file.name}: already {bits}-bit")
        except Exception as e:
            print(f"✗ Error processing {wav_file.name}: {e}")
