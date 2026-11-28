"""Add a book (or more pages of one), run on the laptop: each word gets its sounds (IPA) and a
spoken practice clip, uploaded to Supabase Storage, where the cloud's /v1/assess, /v1/word_audio and /v1/book read them.

    python3 tools/add_book.py tools/example_book.json   # only words without a clip yet are spoken
    python3 tools/add_book.py book.json --force         # speak every word of these pages again

book.json = {"uid": "04A1B2C3", "title": "...", "language": "en-gb", "pages": {"1": "Three green trees.", ...}}
(uid = the tag UID the RFID reader prints; a page given again replaces the stored one.)

Setup (once, any folder outside the repo; about 600 MB):
    uv venv -p 3.12 ~/lumpi-tts && source ~/lumpi-tts/bin/activate
    uv pip install piper-tts numpy scipy transformers phonemizer espeakng_loader
    set -a; source ~/.lumpi-supabase.env; set +a       # SUPABASE_URL, SUPABASE_SECRET_KEY
"""
import argparse
import functools
import io
import json
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "api"))
from _plumbing import UID, WORD, load_book, load_clip, make_bucket, save_book, save_clip  # noqa: E402
from lumpi import norm  # noqa: E402

VOICE = "en_GB-jenny_dioco-medium"  # British voice the grading model heard best (14 of 22 test words)
RATE = 0.85                         # below 1 = slower, clearer for a beginner
MODEL_ID = "facebook/wav2vec2-xlsr-53-espeak-cv-ft"  # the grading model: its tokenizer writes the IPA


def add_book(request, tok, get_voice, force=False):
    """Add the request's pages to the stored book and upload the missing clips.
    tok, get_voice: the tokenizer and a function giving the Piper voice (tests pass fakes)."""
    uid, language, pages = check(request)
    make_bucket()
    book = load_book(uid) or {"title": request["title"], "language": language, "passages": {}}
    book["title"] = request["title"]
    for page, words in pages.items():
        book["passages"][page] = {"text": request["pages"][page].strip(),
                                  "words": [{"word": w, "ipa": ipa_of(tok, w, language)} for w in words]}
        print(f"page {page}: " + "  ".join(f"{w['word']} /{w['ipa']}/" for w in book["passages"][page]["words"]))
    for word in sorted({w for words in pages.values() for w in words}):  # each word spoken once
        if force or load_clip(language, word) is None:
            save_clip(language, word, to_wav(speak(get_voice(), word)))
            print(f"  spoke {word}")
    save_book(uid, book)  # last, so a stored page never names a clip that is not uploaded yet
    return book


@functools.cache  # a long book repeats words thousands of times; work each out once
def ipa_of(tok, word, language):
    """'three' -> 'θ ɹ iː', written by the grading model's own tokenizer ("|" = word end, not a sound)."""
    ipa = " ".join(p for p in tok.phonemize(word, phonemizer_lang=language).split() if p != "|")
    if not norm(ipa.split()):
        raise ValueError(f"no sounds for {word!r}; the grader could never pass it")
    return ipa


def speak(voice, word):
    """Word text -> int16 samples at 16 kHz, silence cut off both ends."""
    import numpy as np
    from piper import SynthesisConfig
    from scipy.signal import resample_poly
    y = np.concatenate([c.audio_float_array for c in voice.synthesize(word, SynthesisConfig(length_scale=1 / RATE))])
    loud = np.flatnonzero(np.abs(y) > 0.02)
    y = y[max(loud[0] - 800, 0): loud[-1] + 800]  # keep a short 800-sample cushion
    y = resample_poly(y, 16000, voice.config.sample_rate)
    return (y / np.abs(y).max() * 0.9 * 32767).astype(np.int16)  # loudest sample at 90% (100% would clip)


def to_wav(samples):
    """int16 samples -> WAV file bytes: 16 kHz, 16-bit, mono (the doll plays nothing else)."""
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(samples.tobytes())
    return out.getvalue()


# ---- Setup and checks ------------------------------------------------------------------------

def check(request):
    """The whole file is checked before anything is uploaded. Returns (uid, language, {page: words})."""
    if not UID.fullmatch(str(request.get("uid"))) or request.get("language") != "en-gb" or not request.get("title"):
        raise ValueError('need "uid" (uppercase hex), "title" and "language": "en-gb"')
    pages = {}
    for page, text in request["pages"].items():
        if not page.isdigit() or page != str(int(page)):
            raise ValueError(f"page numbers are written like 7, not {page!r}")
        pages[page] = split_words(page, text)
    return request["uid"], request["language"], pages


def split_words(page, text):
    """Page text -> the words a child reads, in order. "don't" is one word; spaces, hyphens and
    punctuation only separate words. Digits or other letters are refused: write numbers as words."""
    text = text.lower().replace("’", "'").replace("‘", "'")
    words = WORD.findall(text)
    leftover = set(WORD.sub(" ", text)) - set(" \t\n'-.,!?;:\"()–—“”")
    if leftover or not 0 < len(words) <= 500:
        raise ValueError(f"page {page}: 1-500 words of letters only (found {''.join(sorted(leftover))!r})")
    return words


def tokenizer():
    import espeakng_loader  # ships the espeak-ng library phonemizer needs, no system install
    from phonemizer.backend.espeak.wrapper import EspeakWrapper
    from transformers import Wav2Vec2PhonemeCTCTokenizer
    EspeakWrapper.set_library(espeakng_loader.get_library_path())
    EspeakWrapper.set_data_path(espeakng_loader.get_data_path())
    return Wav2Vec2PhonemeCTCTokenizer.from_pretrained(MODEL_ID)


def load_voice(folder):
    """The Piper voice, downloaded into folder the first time (about 80 MB)."""
    from piper import PiperVoice
    from piper.download_voices import download_voice
    folder = Path(folder).expanduser()
    folder.mkdir(parents=True, exist_ok=True)
    if not (folder / f"{VOICE}.onnx").exists():
        download_voice(VOICE, folder)
    return PiperVoice.load(folder / f"{VOICE}.onnx")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Add a book's pages, sounds and word clips to Supabase.")
    ap.add_argument("book", help="book JSON file, see tools/example_book.json")
    ap.add_argument("--force", action="store_true", help="speak and upload the words of these pages again")
    ap.add_argument("--voices", default="~/lumpi-tts/voices", help="folder for the downloaded Piper voice")
    args = ap.parse_args()
    request = json.loads(Path(args.book).read_text(encoding="utf-8"))
    # functools.cache: the voice is loaded once, on the first clip really needed
    add_book(request, tokenizer(), functools.cache(lambda: load_voice(args.voices)), args.force)
