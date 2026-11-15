import math
import struct

from opus_codec import OpusDecoder, OpusEncoder


def tone(sample_rate=16000, frame_ms=20, frequency=440):
    sample_count = sample_rate * frame_ms // 1000
    values = []
    for index in range(sample_count):
        value = int(8000 * math.sin(2 * math.pi * frequency * index / sample_rate))
        values.append(struct.pack("<h", value))
    return b"".join(values)


encoder = OpusEncoder(sample_rate=16000, channels=1, frame_ms=20, bitrate=20000)
decoder = OpusDecoder(sample_rate=16000, channels=1, max_frame_ms=60)
try:
    packets = encoder.feed(tone())
    assert len(packets) == 1
    assert 0 < len(packets[0]) <= 1275
    decoded = decoder.decode(packets[0])
    assert len(decoded) == 640
    print("OPUS_CODEC_OK packet_bytes={} pcm_bytes={}".format(len(packets[0]), len(decoded)))
finally:
    encoder.close()
    decoder.close()
