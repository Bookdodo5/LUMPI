"""Mock pronunciation AI with the same request and answer shape as the real one (ai/app.py).

No model: it "hears" each third of the recording by how loud it is, so a test can
choose the answer by choosing the sound. It looks up the request's uid and passage in
the same books file as the API: LUMPI_BOOKS, default the test book tests/fixtures/books.json
(uid FFFFFFFF, passage 1: "The fish swims."). The first three words of the passage:
    loud third  -> that word's sounds, read right
    quiet third -> "ʔ" for each sound of that word (all wrong)
    under 0.5 s -> nothing heard
The cloud API (api/index.py grade()) turns that into the doll's command, e.g. loud, loud, quiet -> "2,2".

Runpod serverless "runsync" shape:
    POST /v2/<endpoint>/runsync   Authorization: Bearer <key>
    {"input": {"audio_b64": ..., "uid": ..., "passage": ..., "attempt": ...}}
 -> {"status": "COMPLETED", "output": {"phones": ["ð", "ə", ...], "timing_ms": {...}}}

Run alone:  python3 tests/mock_ai.py   (port 8091, key "mockkey")
"""
import base64
import json
import os
import struct
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

KEY = "mockkey"
BOOKS = Path(os.environ.get("LUMPI_BOOKS", Path(__file__).resolve().parent / "fixtures" / "books.json"))
LOUD = 5000  # average sample size that counts as "read right"


def loudness(samples):
    """Average size of the samples, 0-32767."""
    return sum(abs(s) for s in samples) / max(1, len(samples))


def transcribe(audio, uid="FFFFFFFF", passage=1):
    """The sounds the mock "heard", like ai/app.py assess()["phones"]."""
    book = json.loads(BOOKS.read_text("utf-8")).get(uid)
    if book is None or str(passage) not in book["passages"] or len(audio) < 16000:  # unknown book, or under 0.5 s
        return []
    words = book["passages"][str(passage)]["words"][:3]
    samples = struct.unpack(f"<{len(audio) // 2}h", audio[: len(audio) // 2 * 2])
    third = len(samples) // 3
    phones = []
    for i, word in enumerate(words):
        right = loudness(samples[i * third:(i + 1) * third]) >= LOUD
        phones += [phone if right else "ʔ" for phone in word["ipa"].split()]
    return phones


class MockAi(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.headers.get("Authorization") != "Bearer " + KEY:
            self.send_response(401)
            self.end_headers()
            return
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        given = body["input"]
        output = {"phones": transcribe(base64.b64decode(given["audio_b64"]), given.get("uid"), given.get("passage")),
                  "timing_ms": {}}
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
    assert transcribe(tone(3, [True, True, True])) == ["ð", "ə", "f", "ɪ", "ʃ", "s", "w", "ɪ", "m", "z"]
    assert transcribe(tone(3, [True, True, False]))[-5:] == ["ʔ"] * 5
    assert transcribe(tone(0.2, [True, True, True])) == []
    print("mock AI self-check OK; serving on http://localhost:8091")
    HTTPServer(("0.0.0.0", 8091), MockAi).serve_forever()
