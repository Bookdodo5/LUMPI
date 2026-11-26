"""LUMPI pronunciation AI as a Runpod serverless endpoint (Runpod SDK 2.0, `rp flash`).

It only TRANSCRIBES: it writes down the IPA sounds it hears, with no hint of the text.
Grading is done by the cloud API (api/lumpi.py grade()). Same method as
Embed/ipa_pronunciation_check.ipynb sound_to_ipa().

Called by api/_plumbing.py ask_ai() with {"input": {"audio_b64": ...}} (16 kHz, 16-bit, mono PCM). Answers:
    {"phones": ["θ", "ɹ", "iː", ...], "timing_ms": {"decode", "model", "threads", "audio_s"}}

Deploy:   rp login   (once)   then   rp flash deploy ai
"""
import runpod

app = runpod.App("lumpi-ai")
MODEL_ID = "facebook/wav2vec2-xlsr-53-espeak-cv-ft"


@app.queue(
    cpu="cpu3c-8-16",  # 8 vCPU compute-optimised: ~30-40% faster than 4 vCPU (measured)
    datacenter=["AP-JP-1", "US-CA-2"],  # Japan is closest to Thailand; California when Japan has no stock
    workers=(0, 1),  # 0 = costs nothing while idle; 1 = one reading at a time
    idle_timeout=30,
    # ponytail: the model (1.3 GB) downloads at each cold start; a network volume would keep it.
    # CPU build of torch (~180 MB); plain "torch" pulls ~2 GB of GPU libraries.
    dependencies=["torch @ https://download.pytorch.org/whl/cpu/torch-2.7.1%2Bcpu-cp312-cp312-manylinux_2_28_x86_64.whl",
                  "transformers", "numpy"],
)
def assess(audio_b64: str, uid: str = "", passage: int = 0, attempt: int = 0):
    """Transcribe one recording into the IPA sounds the model heard."""
    import base64
    import time

    import numpy as np
    import torch

    # Set again here: the setting in load_model() does not reach the thread that runs requests.
    torch.set_num_threads(8)

    start = time.perf_counter()
    raw = base64.b64decode(audio_b64)
    pcm = np.frombuffer(raw[: len(raw) // 2 * 2], dtype="<i2").astype(np.float32) / 32768.0
    # Zero mean, unit variance (as the notebook's processor does), so quiet and loud readings look the same.
    pcm = (pcm - pcm.mean()) / np.sqrt(pcm.var() + 1e-7) if len(pcm) else pcm
    decoded = time.perf_counter()

    phones = []
    if len(pcm) >= 1600:  # under 0.1 s there is nothing to hear
        with torch.inference_mode():
            ids = MODEL(torch.from_numpy(pcm)[None]).logits[0].argmax(-1).tolist()
        previous = None
        for i in ids:  # CTC rule: merge repeats, then drop the blank <pad> and the other <...> tokens
            if i != previous and not TOKENS[i].startswith("<"):
                phones.append(TOKENS[i])
            previous = i
    heard = time.perf_counter()

    return {
        "phones": phones,
        "timing_ms": {"decode": round((decoded - start) * 1000), "model": round((heard - decoded) * 1000),
                      "threads": torch.get_num_threads(), "audio_s": round(len(pcm) / 16000, 2)},
    }


@assess.init
def load_model():
    """Once per worker, before it takes readings: load the model and its list of sounds."""
    global MODEL, TOKENS
    import json

    import torch
    from huggingface_hub import hf_hub_download
    from transformers import Wav2Vec2ForCTC

    # 8 = the worker's vCPUs. Not os.cpu_count(): on Runpod that is the whole host (256), ~2x slower.
    torch.set_num_threads(8)
    MODEL = Wav2Vec2ForCTC.from_pretrained(MODEL_ID).eval()
    # vocab.json = {sound: id}, read directly (the notebook's processor would need espeak-ng installed).
    vocab = json.loads(open(hf_hub_download(MODEL_ID, "vocab.json"), encoding="utf-8").read())
    TOKENS = {i: sound for sound, i in vocab.items()}
    # Practice run: torch's first call is ~4 s slower. Pay it here, not on a child's first reading.
    with torch.inference_mode():
        MODEL(torch.zeros(1, 16000))
