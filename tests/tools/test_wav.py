#!/usr/bin/env python3
"""Create a minimal test WAV file."""
import struct
import math

# Generate 1 second of 440Hz sine wave at 44.1kHz, 16-bit, mono
sample_rate = 44100
duration = 1.0
frequency = 440.0

samples = []
for i in range(int(sample_rate * duration)):
    t = i / sample_rate
    sample = int(32767 * 0.5 * math.sin(2 * math.pi * frequency * t))
    samples.append(sample)

# Write WAV file
data = struct.pack('<' + 'h' * len(samples), *samples)
data_size = len(data)

with open('res/sfx/test_tone.wav', 'wb') as f:
    # RIFF header
    f.write(b'RIFF')
    f.write(struct.pack('<I', 36 + data_size))  # file size - 8
    f.write(b'WAVE')

    # fmt chunk
    f.write(b'fmt ')
    f.write(struct.pack('<I', 16))  # fmt chunk size
    f.write(struct.pack('<H', 1))   # PCM
    f.write(struct.pack('<H', 1))   # mono
    f.write(struct.pack('<I', sample_rate))
    f.write(struct.pack('<I', sample_rate * 2))  # byte rate
    f.write(struct.pack('<H', 2))   # block align
    f.write(struct.pack('<H', 16))  # 16-bit

    # data chunk
    f.write(b'data')
    f.write(struct.pack('<I', data_size))
    f.write(data)

print(f"✓ Created test_tone.wav: 16-bit mono @ {sample_rate}Hz, {duration}s 440Hz sine wave")
