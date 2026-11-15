import ctypes
import ctypes.util


OPUS_APPLICATION_VOIP = 2048
OPUS_SET_BITRATE_REQUEST = 4002
OPUS_SET_VBR_REQUEST = 4006
OPUS_SET_COMPLEXITY_REQUEST = 4010


class OpusError(RuntimeError):
    pass


def _load_opus():
    names = []
    discovered = ctypes.util.find_library("opus")
    if discovered:
        names.append(discovered)
    names.extend(["libopus.so.0", "libopus.so", "opus.dll", "libopus-0.dll"])
    last_error = None
    for name in names:
        try:
            library = ctypes.CDLL(name)
            break
        except OSError as exc:
            last_error = exc
    else:
        raise OpusError("libopus is not installed: {}".format(last_error))

    library.opus_strerror.argtypes = [ctypes.c_int]
    library.opus_strerror.restype = ctypes.c_char_p
    library.opus_encoder_create.argtypes = [
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
    ]
    library.opus_encoder_create.restype = ctypes.c_void_p
    library.opus_encoder_destroy.argtypes = [ctypes.c_void_p]
    library.opus_encode.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_ubyte),
        ctypes.c_int32,
    ]
    library.opus_encode.restype = ctypes.c_int32
    library.opus_encoder_ctl.argtypes = [ctypes.c_void_p, ctypes.c_int]
    library.opus_encoder_ctl.restype = ctypes.c_int
    library.opus_decoder_create.argtypes = [
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
    ]
    library.opus_decoder_create.restype = ctypes.c_void_p
    library.opus_decoder_destroy.argtypes = [ctypes.c_void_p]
    library.opus_decode.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_ubyte),
        ctypes.c_int32,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_int,
        ctypes.c_int,
    ]
    library.opus_decode.restype = ctypes.c_int
    return library


def _check(library, result, operation):
    if result < 0:
        message = library.opus_strerror(result).decode("utf-8", "replace")
        raise OpusError("{} failed: {}".format(operation, message))


class OpusEncoder:
    def __init__(self, sample_rate=16000, channels=1, frame_ms=20, bitrate=20000):
        self.sample_rate = sample_rate
        self.channels = channels
        self.frame_ms = frame_ms
        self.frame_samples = sample_rate * frame_ms // 1000
        self.frame_bytes = self.frame_samples * channels * 2
        self._buffer = bytearray()
        self._library = _load_opus()
        error = ctypes.c_int()
        self._handle = self._library.opus_encoder_create(
            sample_rate,
            channels,
            OPUS_APPLICATION_VOIP,
            ctypes.byref(error),
        )
        _check(self._library, error.value, "opus_encoder_create")
        if not self._handle:
            raise OpusError("opus_encoder_create returned null")
        for request, value in (
            (OPUS_SET_BITRATE_REQUEST, bitrate),
            (OPUS_SET_VBR_REQUEST, 1),
            (OPUS_SET_COMPLEXITY_REQUEST, 5),
        ):
            result = self._library.opus_encoder_ctl(self._handle, request, value)
            _check(self._library, result, "opus_encoder_ctl")

    def feed(self, pcm):
        self._buffer.extend(pcm)
        packets = []
        while len(self._buffer) >= self.frame_bytes:
            frame = bytes(self._buffer[: self.frame_bytes])
            del self._buffer[: self.frame_bytes]
            samples = (ctypes.c_int16 * (self.frame_samples * self.channels)).from_buffer_copy(frame)
            output = (ctypes.c_ubyte * 1275)()
            length = self._library.opus_encode(
                self._handle,
                samples,
                self.frame_samples,
                output,
                len(output),
            )
            _check(self._library, length, "opus_encode")
            packets.append(bytes(output[:length]))
        return packets

    def close(self):
        if self._handle:
            self._library.opus_encoder_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()


class OpusDecoder:
    def __init__(self, sample_rate=16000, channels=1, max_frame_ms=60):
        self.sample_rate = sample_rate
        self.channels = channels
        self.max_frame_samples = sample_rate * max_frame_ms // 1000
        self._library = _load_opus()
        error = ctypes.c_int()
        self._handle = self._library.opus_decoder_create(
            sample_rate,
            channels,
            ctypes.byref(error),
        )
        _check(self._library, error.value, "opus_decoder_create")
        if not self._handle:
            raise OpusError("opus_decoder_create returned null")

    def decode(self, packet):
        if not packet or len(packet) > 1275:
            raise OpusError("invalid Opus packet length")
        encoded = (ctypes.c_ubyte * len(packet)).from_buffer_copy(packet)
        output = (ctypes.c_int16 * (self.max_frame_samples * self.channels))()
        samples = self._library.opus_decode(
            self._handle,
            encoded,
            len(packet),
            output,
            self.max_frame_samples,
            0,
        )
        _check(self._library, samples, "opus_decode")
        byte_count = samples * self.channels * 2
        return ctypes.string_at(output, byte_count)

    def close(self):
        if self._handle:
            self._library.opus_decoder_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()
