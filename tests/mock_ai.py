"""Mock reading AI with the same request and answer shape as the real one on Runpod.

No model: it "grades" each third of the recording by how loud it is, so a test
can choose the answer by choosing the sound.
    loud third  -> that word scores about 90 (read well)
    quiet third -> that word scores about 20 (read badly)

Runpod serverless "runsync" shape:
    POST /v2/<endpoint>/runsync   Authorization: Bearer <key>
    {"input": {"audio_b64": ..., "uid": ..., "passage": ..., "attempt": ...}}
 -> {"status": "COMPLETED", "output": {"code": 1-4, "word": 0-2, "score": 0-100, "words": [...]}}

Run alone:  python3 tests/mock_ai.py   (port 8091, key "mockkey")
"""
import base64
import json
import struct
from http.server import BaseHTTPRequestHandler, HTTPServer

KEY = "mockkey"
WORDS = ["the", "fish", "swims"]  # passage words; the real AI reads them from the book
PASS_SCORE = 60  # same rule as the guide's decide(): a word passes at 60 or more


def loudness(samples):
    """Average size of the samples, 0-32767."""
    return sum(abs(s) for s in samples) / max(1, len(samples))


def grade(audio):
    if len(audio) < 16000:  # under 0.5 s: too short to judge
        return {"code": 3, "word": 0, "score": 0, "words": [{"word": w, "score": 0, "phonemes": []} for w in WORDS]}
    samples = struct.unpack(f"<{len(audio) // 2}h", audio[: len(audio) // 2 * 2])
    third = len(samples) // 3
    words = []
    for i, word in enumerate(WORDS):
        score = min(100, round(loudness(samples[i * third:(i + 1) * third]) / 100))
        words.append({"word": word, "score": score, "phonemes": [{"ipa": "ɪ", "score": score}]})
    failing = [i for i, w in enumerate(words) if w["score"] < PASS_SCORE]
    total = round(sum(w["score"] for w in words) / len(words))
    if not failing:
        return {"code": 1, "word": 0, "score": total, "words": words}
    worst = min(failing, key=lambda i: words[i]["score"])
    return {"code": 2, "word": worst, "score": total, "words": words}


class MockAi(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.headers.get("Authorization") != "Bearer " + KEY:
            self.send_response(401)
            self.end_headers()
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        output = grade(base64.b64decode(body["input"]["audio_b64"]))
        reply = json.dumps({"id": "mock-job", "status": "COMPLETED", "output": output}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def log_message(self, format, *args):
        pass


def tone(seconds, loud_thirds):
    """A fake recording: 16 kHz PCM16, each third loud (True) or quiet (False)."""
    count = int(16000 * seconds)
    out = []
    for n in range(count):
        size = 9000 if loud_thirds[min(2, n * 3 // count)] else 2000
        out.append(size if n % 16 < 8 else -size)  # a 1 kHz square wave
    return struct.pack(f"<{count}h", *out)


if __name__ == "__main__":
    assert grade(tone(3, [True, True, True]))["code"] == 1
    assert (grade(tone(3, [True, True, False]))["code"], grade(tone(3, [True, True, False]))["word"]) == (2, 2)
    assert grade(tone(3, [True, False, True]))["word"] == 1
    assert grade(tone(0.2, [True, True, True]))["code"] == 3
    print("mock AI self-check OK; serving on http://localhost:8091")
    HTTPServer(("0.0.0.0", 8091), MockAi).serve_forever()
