"""A doll played by the laptop: tests s5_nest without a doll board (stage 5, 7, 8).

Does the same steps as src/bench/5_doll_nest/5_doll.cpp: presence, one reading
(start -> chunks -> finish -> poll attempt), help, noise. Prints PASS/FAIL per step
and the time from end of recording to the result (goal under 4 s).

    LUMPI_LAN_KEY=<key> python3 tests/fake_doll.py http://<nest address> [--node 3] [--attempt 7]

The audio is a tone, loud in all three thirds, so tests/mock_ai.py grades it "accepted".
Use --node 3 so it does not clash with a real doll 1 or 2.
"""
import argparse
import math
import os
import struct
import sys
import time
import urllib.error
import urllib.request

CHUNK = 2048            # Session::max_chunk_bytes
SECONDS = 3             # the bench nest keeps at most 3 s


def call(base, key, method, route, query, body=None, timeout=15):
    request = urllib.request.Request(f"{base}{route}?{query}", data=body, method=method,
                                     headers={"Authorization": f"Bearer {key}"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as reply:
            return reply.status, reply.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()
    except (urllib.error.URLError, TimeoutError) as error:
        return -1, str(error)


def tone(seconds):
    samples = (int(12000 * math.sin(2 * math.pi * 440 * i / 16000)) for i in range(16000 * seconds))
    return b"".join(struct.pack("<h", s) for s in samples)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("nest", help="e.g. http://192.168.1.50")
    parser.add_argument("--node", type=int, default=3)
    parser.add_argument("--attempt", type=int, default=int(time.time()) % 100000)
    args = parser.parse_args()
    key = os.environ.get("LUMPI_LAN_KEY", "")
    if not key:
        sys.exit("set LUMPI_LAN_KEY (same as config/private.h)")
    base, node, attempt = args.nest.rstrip("/"), args.node, args.attempt
    failures = 0

    def check(name, ok, detail):
        nonlocal failures
        failures += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
        return ok

    code, text = call(base, "wrong-key", "POST", "/v1/presence", f"node={node}&present=1")
    check("wrong key refused", code == 401, f"{code} {text}")
    code, text = call(base, key, "POST", "/v1/presence", f"node={node}&present=1&fsr=2500")
    if not check("presence (book on)", code == 200 and text == "ok", f"{code} {text}"):
        sys.exit("nest not reachable or key wrong: stop")

    audio = tone(SECONDS)
    recorded = time.time()
    code, text = call(base, key, "POST", "/v1/start", f"node={node}&attempt={attempt}&passage=1&uid=04A1B2C3&fsr=2500")
    check("start", code == 200 and text == "ok", f"{code} {text}")
    chunks = [audio[i:i + CHUNK] for i in range(0, len(audio), CHUNK)]
    acked = all(call(base, key, "PUT", "/v1/chunk", f"node={node}&attempt={attempt}&index={i}", c) == (200, str(i))
                for i, c in enumerate(chunks))
    check("chunks ACKed", acked, f"{len(chunks)} chunks, {len(audio)} bytes")
    # A re-send (the doll lost our ACK) is ACKed again but not stored twice. Only while recording.
    code, text = call(base, key, "PUT", "/v1/chunk", f"node={node}&attempt={attempt}&index={len(chunks) - 1}", chunks[-1])
    check("re-sent chunk ACKed again", (code, text) == (200, str(len(chunks) - 1)), f"{code} {text}")
    code, text = call(base, key, "POST", "/v1/finish", f"node={node}&attempt={attempt}&chunks={len(chunks)}&bytes={len(audio)}")
    check("finish", code == 200 and text == "assessing", f"{code} {text}")

    uploaded = time.time()
    text = ""
    while time.time() - uploaded < 20:
        code, text = call(base, key, "GET", "/v1/attempt", f"node={node}&attempt={attempt}")
        if code != 200 or text.startswith("result,"):
            break
        time.sleep(0.3)
    done = time.time()
    check("result", text.startswith("result,"), f"{text}  (upload {uploaded - recorded:.2f} s + grading "
                                                 f"{done - uploaded:.2f} s = {done - recorded:.2f} s, goal < 4 s)")
    code, text = call(base, key, "GET", "/v1/attempt", f"node={node}&attempt={attempt - 1}")
    check("old attempt refused", code == 409, f"{code} {text}")

    code, text = call(base, key, "GET", "/v1/help", f"node={node}")
    check("help", code == 200 and text in ["waiting", "coming"], f"{code} {text} (press ACK on the nest for 'coming')")
    code, text = call(base, key, "POST", "/v1/noise", f"node={node}&db=65.0")
    check("noise", code == 200, f"{code} {text}")
    code, text = call(base, key, "GET", "/v1/status", "")
    check("status", code == 200 and '"dolls"' in text, f"{code} {text[:120]}")

    print(f"\n{'all passed' if failures == 0 else f'{failures} failed'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
