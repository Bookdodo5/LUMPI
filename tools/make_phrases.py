"""Speak the doll's phrases into data_doll/phrases/ (16 kHz 16-bit mono WAV), in the same voice as
the word clips. The names match the command table in src/app/station/doll.cpp.

    ~/lumpi-tts/bin/python tools/make_phrases.py       (setup: see tools/add_book.py)

Then put them on the doll: PlatformIO > env doll > Platform > Upload Filesystem Image.
"""
from pathlib import Path

from add_book import load_voice, speak, to_wav

PHRASES = {
    "well_done": "Well done!",
    "try_word": "Let's practise this word.",
    "move_closer": "Please move closer and read again.",
    "try_again": "Let us try again.",
    "shh": "Shh. Quiet, please.",
    "too_dark": "Please turn on the light.",
    "told_teacher": "I have told your teacher.",
    "teacher_coming": "Your teacher is coming.",
}

if __name__ == "__main__":
    folder = Path(__file__).resolve().parent.parent / "data_doll" / "phrases"
    folder.mkdir(parents=True, exist_ok=True)
    voice = load_voice("~/lumpi-tts/voices")
    for name, text in PHRASES.items():
        (folder / f"{name}.wav").write_bytes(to_wav(speak(voice, text)))
        print(f"{name}.wav  {text}")
