"""LUMPI reading AI as a Runpod serverless endpoint (Runpod SDK 2.0, `rp flash`).

Called by api/index.py ask_ai() with the Runpod "runsync" body:
    {"input": {"audio_b64": ..., "uid": ..., "passage": ..., "attempt": ...}}
Answers the same shape as tests/mock_ai.py: {"code", "word", "score", "words", "timing_ms"}.

STATUS: timing build. The model and its cost are real (load once per worker, full
forward pass on every recording). The per-word score is a placeholder from the
model's confidence; the real forced alignment against the passage IPA comes from
reading_buddy_live_reading_v2.ipynb next.

Deploy:   rp login   (once)   then   rp flash deploy ai
"""
import runpod

app = runpod.App("lumpi-ai")
MODEL_ID = "facebook/wav2vec2-xlsr-53-espeak-cv-ft"


@app.queue(
    cpu="cpu3c-8-16",  # 8 vCPU compute-optimized: measured on a laptop as ~30-40% faster than 4 vCPU
    # Japan is closest to Thailand / Vercel Singapore, but its CPU stock comes and goes
    # (none on 2026-11-19). California is the backup, so a worker can always start.
    datacenter=["AP-JP-1", "US-CA-2"],
    workers=(0, 1),  # 0 = costs nothing while idle; 1 = one reading at a time
    idle_timeout=30,
    # ponytail: the model (1.3 GB) downloads at each cold start (billed start time).
    # Runpod's model cache (model=runpod.Model) is GPU-only: on this CPU endpoint the
    # files never appeared (tested 2026-11-19). Upgrade: a network volume holding the model.
    # CPU build of torch (~180 MB). Plain "torch" pulls the GPU build + ~2 GB of CUDA libraries.
    dependencies=["torch @ https://download.pytorch.org/whl/cpu/torch-2.7.1%2Bcpu-cp312-cp312-manylinux_2_28_x86_64.whl",
                  "transformers", "numpy"],
)
# float32 by default: on Runpod with 8 threads it graded a 3 s reading in 454-483 ms,
# steadier than int8 (298-2368 ms) and with no accuracy risk (measured 2026-11-19).
def assess(audio_b64: str, uid: str, passage: int, attempt: int, precision: str = "float32"):
    import base64
    import time

    import numpy as np
    import torch

    # Again here: the setting in load_model() did not reach the thread that runs
    # requests (it reported 128 threads on Runpod). 8 = the worker's vCPUs.
    torch.set_num_threads(8)

    start = time.perf_counter()
    pcm = np.frombuffer(base64.b64decode(audio_b64), dtype="<i2").astype(np.float32) / 32768.0
    audio = torch.from_numpy(pcm)[None]
    decoded = time.perf_counter()

    model = MODELS[precision]
    with torch.inference_mode():
        probabilities = model(audio).logits.softmax(-1)[0]
    graded = time.perf_counter()

    # Placeholder scoring: split the recording into 3 words, score = model confidence.
    words = []
    for i, part in enumerate(probabilities.chunk(3)):
        score = round(float(part.max(-1).values.mean()) * 100)
        words.append({"word": f"word{i}", "score": score, "phonemes": []})
    failing = [i for i, w in enumerate(words) if w["score"] < 60]
    code, word = (1, 0) if not failing else (2, min(failing, key=lambda i: words[i]["score"]))

    return {
        "code": code,
        "word": word,
        "score": round(sum(w["score"] for w in words) / 3),
        "words": words,
        "timing_ms": {"decode": round((decoded - start) * 1000), "model": round((graded - decoded) * 1000),
                      "threads": torch.get_num_threads(), "precision": precision, "audio_s": round(len(pcm) / 16000, 2)},
    }


@assess.init
def load_model():
    """Once per worker, before it takes readings: load the model and make the int8 copy."""
    global MODELS
    import torch
    from transformers import Wav2Vec2ForCTC

    # Same number as cpu="cpu3c-8-16". NOT os.cpu_count(): on Runpod that is the whole
    # host (256 cores, measured), and 256 threads on 8 vCPUs made grading ~2x slower.
    torch.set_num_threads(8)
    model = Wav2Vec2ForCTC.from_pretrained("facebook/wav2vec2-xlsr-53-espeak-cv-ft").eval()
    MODELS = {"float32": model, "int8": torch.quantization.quantize_dynamic(model, {torch.nn.Linear}, dtype=torch.qint8)}
    # Practice run: torch's first call is ~4 s slower (measured locally). Pay it here,
    # once per worker, not on a child's first reading.
    with torch.inference_mode():
        for warm in MODELS.values():
            warm(torch.zeros(1, 16000))
