import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app import synthesize_speech


OUTPUT_PATH = os.getenv(
    "TEST_AUDIO_PATH",
    "/opt/smart-elderly-voice/public/caregiver-speaker-test.wav",
)
TEXT = (
    "您好，我是您的智能看护员。"
    "现在进行第二次扬声器测试。"
    "如果您能听到这段新语音，说明硬件已经重新下载并正常播放。"
)


def main():
    audio, content_type, extension = synthesize_speech(TEXT)
    if content_type != "audio/wav" or extension != "wav":
        raise RuntimeError(
            "扬声器测试音频必须为 WAV，实际为 {}/{}".format(content_type, extension)
        )
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    temporary_path = OUTPUT_PATH + ".tmp"
    with open(temporary_path, "wb") as handle:
        handle.write(audio)
    os.replace(temporary_path, OUTPUT_PATH)
    print("{} {} bytes".format(OUTPUT_PATH, len(audio)))


if __name__ == "__main__":
    main()
