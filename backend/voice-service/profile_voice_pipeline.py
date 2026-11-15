import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app import generate_reply, synthesize_speech, transcribe_audio


INPUT_PATH = os.getenv(
    "PROFILE_AUDIO_PATH",
    "/tmp/smart-elderly-voice-test/input.wav",
)


def measure(name, callback):
    started = time.perf_counter()
    result = callback()
    elapsed = time.perf_counter() - started
    print("{}_SECONDS={:.3f}".format(name, elapsed))
    return result, elapsed


def main():
    with open(INPUT_PATH, "rb") as handle:
        audio = handle.read()

    transcript, asr_seconds = measure(
        "ASR",
        lambda: transcribe_audio(audio, "profile.wav", "audio/wav"),
    )
    reply, ai_seconds = measure(
        "AI",
        lambda: generate_reply("latency-profile", transcript),
    )
    synthesized, tts_seconds = measure(
        "TTS",
        lambda: synthesize_speech(reply),
    )
    reply_audio, content_type, extension = synthesized

    print("TOTAL_UPSTREAM_SECONDS={:.3f}".format(asr_seconds + ai_seconds + tts_seconds))
    print("INPUT_BYTES={}".format(len(audio)))
    print("TRANSCRIPT={}".format(transcript))
    print("REPLY={}".format(reply))
    print("OUTPUT_BYTES={}".format(len(reply_audio)))
    print("OUTPUT_TYPE={}/{}".format(content_type, extension))


if __name__ == "__main__":
    main()
