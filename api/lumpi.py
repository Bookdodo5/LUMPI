"""LUMPI cloud: what happens to each message, in the order of the story.

    1. POST /api/telemetry   the nest sends an event (room, start, status, help, noise) -> store it
    2. POST /v1/assess       the nest sends a recording -> AI hears sounds -> compare with the book -> reply
    3. GET  /v1/page, /v1/word_audio   a page's words, one word's practice clip
    4. GET  /api/dashboard/{nodes,history,scores}   what the teacher's dashboard shows

The web server, passwords, Supabase calls and input checks are in _plumbing.py
(you do not need to read it to understand LUMPI).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # so Vercel finds _plumbing.py next to this file
from _plumbing import Handler, ask_ai, db_get, db_insert, load_book, load_clip  # noqa: E402

READING = ["book_on", "recording", "waiting_ai", "feedback"]  # a doll in these is in a reading session
PASS_SCORE = 60  # a word passes at 60 or more


# ---- 1. The nest sends an event: store it once ------------------------------------------

def telemetry(event):
    """Returns "added", or "duplicate" if this event_id was stored before (the nest resends)."""
    if event["event_type"] == "score":
        event["session"] = session_start(event["node_id"], event["recorded_at"])
    return db_insert(event)


def session_start(node, score_time):
    """When the doll's reading session began: the oldest of its unbroken run of reading events."""
    rows = db_get(f"events?select=recorded_at,activity&node_id=eq.{node}&event_type=in.(start,status)"
                  f"&recorded_at=lte.{score_time}&order=recorded_at.desc", limit=500)
    start = score_time
    for row in rows:  # newest first
        if row["activity"] not in READING:
            break
        start = row["recorded_at"]
    return start


# ---- 2. The nest sends a recording: grade it ----------------------------------------------

def assess(node, attempt, page, uid, audio):
    """Returns the nest's answer: "code,word" (1 all good, 2 practise word number `word`,
    3 nothing heard; 4 = try again, sent by _plumbing.py if anything fails), and for code 2
    one more line per word to practise: "<index>,<word>,<book sounds>,<clip address>"."""
    book = load_book(uid)
    words = book["passages"][str(page)]["words"]  # [{"word": "three", "ipa": "θ ɹ iː"}, ...]
    heard = ask_ai(audio)                          # the sounds the AI heard, e.g. ["θ", "ɹ", "iː"]
    code, worst, total, scored = grade(words, book["language"], heard)

    score = {"event_id": f"cloud_ai/{node}-{attempt}-{int(time.time() * 1000)}", "node_id": node,
             "event_type": "score", "recorded_at": int(time.time()), "activity": None,
             "data": {"uid": uid, "passage": page, "attempt": attempt, "score": total, "words": scored}}
    try:
        telemetry(score)
    except OSError:
        print(f"score not stored (database down); node {node} still gets its answer")

    answer = f"{code},{worst}"
    if code == 2:
        for i, word in enumerate(scored):
            if word["score"] < PASS_SCORE:
                answer += f"\n{i},{word['word']},{words[i]['ipa']},/v1/word_audio?uid={uid}&passage={page}&word={i}"
    return answer


def grade(book_words, language, phones):
    """Compare the sounds heard with the book's. Returns (code, worst word, total score, words)."""
    words = [(item["word"], norm(item["ipa"].split(), language)) for item in book_words]
    heard = norm(phones, language)
    scored = score_words(words, heard)
    total = round(sum(word["score"] for word in scored) / len(scored))
    if not heard:
        return 3, 0, total, scored
    worst = min(range(len(scored)), key=lambda i: scored[i]["score"])
    if scored[worst]["score"] >= PASS_SCORE:
        return 1, 0, total, scored
    return 2, worst, total, scored


# Every book is British English ("en-gb"). The model writes some sounds differently from the book;
# these are made the same on both sides. No "r" after a vowel, so an American "r" is not marked wrong.
# Keys have no "ː": a long vowel is not a different word.
SAME_EN = {"ɚ": "ə", "ɝ": "ɜ", "ɑɹ": "ɑ", "ɔɹ": "ɔ", "oɹ": "ɔ", "aɪɚ": "aɪ ə",
           "ɪɹ": "ɪ ə", "ɛɹ": "ɛ ə", "ʊɹ": "ʊ ə", "iə": "ɪ ə", "eə": "ɛ ə", "ʊə": "ʊ ə",  # here, there, sure
           "o": "əʊ", "oʊ": "əʊ", "e": "eɪ", "ɐ": "ʌ", "æ": "a",  # accents of "go", "take", "cup", "cat"
           "aɪə": "aɪ ə", "əl": "ə l", "l̩": "ə l", "n̩": "ə n",  # one model token = two sounds
           "ɕ": "ʃ", "tɕ": "tʃ", "kh": "k", "th": "t", "ph": "p", "əɜ": "ə"}  # Mandarin spellings it sometimes writes
NOT_HEARD = str.maketrans("", "", "ˈˌ̞ᵝ̯̚˥˦˧˨˩0123456789ː.")  # stress, fine detail, tones, length, "." = syllable break


def norm(phones, language):
    """Clean a list of sounds so the book and the AI spell the same sound the same way.
    `language` is the book's; only "en-gb" books exist, so the English rules above always apply."""
    out = []
    for phone in phones:
        phone = phone.translate(NOT_HEARD)
        if phone and phone != "|":  # "|" marks a word end, not a sound
            out += SAME_EN.get(phone, phone).split()
    return out


def align(expected, heard):
    """Line up two sound lists with the fewest changes (Levenshtein). Returns pairs (expected, heard):
    (a, a) right, (a, b) said b instead of a, (a, None) missed a, (None, b) extra b."""
    n, m = len(expected), len(heard)
    # cost[i][j] = fewest changes to turn the first i expected sounds into the first j heard ones
    cost = [[i + j if i == 0 or j == 0 else 0 for j in range(m + 1)] for i in range(n + 1)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            change = 0 if expected[i - 1] == heard[j - 1] else 1
            cost[i][j] = min(cost[i - 1][j - 1] + change, cost[i - 1][j] + 1, cost[i][j - 1] + 1)
    pairs, i, j = [], n, m  # walk back from the bottom-right corner
    while i or j:
        if i and j and cost[i][j] == cost[i - 1][j - 1] + (expected[i - 1] != heard[j - 1]):
            pairs.append((expected[i - 1], heard[j - 1]))
            i, j = i - 1, j - 1
        elif i and cost[i][j] == cost[i - 1][j] + 1:
            pairs.append((expected[i - 1], None))
            i -= 1
        else:
            pairs.append((None, heard[j - 1]))
            j -= 1
    return pairs[::-1]


def score_words(words, heard):
    """words = [(word, [sounds])]. Aligns the whole page at once, then cuts the pairs into words.
    Each word: score = sounds right / (book sounds + extra sounds); phonemes = each book sound
    and what was heard instead (None = missed); extra = sounds heard that are not in the book."""
    owner = [k for k, (_, sounds) in enumerate(words) for _ in sounds]  # the word of each book sound
    per_word = [[] for _ in words]
    k, word = 0, 0
    for expected, said in align([s for _, sounds in words for s in sounds], heard):
        if expected is not None:
            word = owner[k]
            k += 1
        per_word[word].append((expected, said))  # an extra sound joins the word before it
    result = []
    for (text, _), pairs in zip(words, per_word):
        right = sum(expected is not None and expected == said for expected, said in pairs)
        result.append({"word": text, "score": round(right * 100 / max(len(pairs), 1)),
                       "phonemes": [{"ipa": e, "score": 100 if e == h else 0, "heard": h} for e, h in pairs if e is not None],
                       "extra": [h for e, h in pairs if e is None]})
    return result


# ---- 3. A page's words and one word's practice clip ----------------------------------------

def page(uid, page):
    book = load_book(uid)
    passage = book["passages"][page]
    words = [{"word": w["word"], "ipa": w["ipa"], "audio": f"/v1/word_audio?uid={uid}&passage={page}&word={i}"}
             for i, w in enumerate(passage["words"])]
    return {"uid": uid, "page": int(page), "title": book["title"], "language": book["language"],
            "text": passage["text"], "words": words}


def word_audio(uid, page, index):
    book = load_book(uid)
    word = book["passages"][page]["words"][index]["word"]
    return load_clip(book["language"], word)  # WAV bytes, or None if the clip was never made


# ---- 4. What the teacher's dashboard shows ---------------------------------------------------

SHOWN_NODES = [0, 1, 2]  # 0 = nest, 1 and 2 = dolls
WINDOW_S = 300           # help popups last 5 minutes


def nodes(now):
    """The latest state of the nest and each doll: online (heard from in the last 30 s), stale, offline."""
    rows = db_get(f"events?select=*&received_at=gte.{now - WINDOW_S}&order=received_at")
    for node in SHOWN_NODES:  # silent for longer than the window: show its last message (offline)
        if not any(row["node_id"] == node for row in rows):
            rows += db_get(f"events?select=*&node_id=eq.{node}&event_type=neq.score&order=received_at.desc", limit=1)
    rows.sort(key=lambda row: row["received_at"])

    latest = {}
    for row in rows:
        if row["event_type"] == "score":
            continue  # scores come from the cloud, they say nothing about the doll
        node = latest.setdefault(row["node_id"], {"node_id": row["node_id"], "recorded_at": 0,
                                                  "activity": None, "values": {}, "help_at": None})
        node["last_seen"] = row["received_at"]
        if row["event_type"] == "help":
            node["help_at"] = max(node["help_at"] or 0, row["recorded_at"])
        elif row["event_type"] != "noise" and row["recorded_at"] >= node["recorded_at"]:
            node["recorded_at"] = row["recorded_at"]  # a late, older message must not undo a newer one
            node["activity"] = row["activity"]
            node["values"].update(row["data"])

    result = []
    for node_id in sorted(set(SHOWN_NODES) | set(latest)):
        node = latest.get(node_id, {"node_id": node_id, "status": "unknown"})
        if node_id in latest:
            age = now - node["last_seen"]
            node["age_s"] = round(age, 1)
            node["status"] = "online" if age < 30 else "stale" if age < 60 else "offline"
            # the child pressed help less than 5 minutes ago (an offline doll shows "last seen" instead)
            node["needs_help"] = (node["status"] != "offline" and node["help_at"] is not None
                                  and now - node["help_at"] < WINDOW_S)
        result.append(node)
    return result


def history(now, minutes):
    """Every nest and doll message of the last minutes, per node (for the mode timelines)."""
    start = now - minutes * 60
    result = {str(node): [] for node in SHOWN_NODES}
    for row in db_get(f"events?select=*&received_at=gte.{start}&order=received_at"):
        # recorded_at too: a message sent late from an earlier day belongs to that day
        if row["event_type"] != "score" and row["recorded_at"] >= start:
            point = {key: row[key] for key in ["recorded_at", "event_type", "activity", "data"]}
            result.setdefault(str(row["node_id"]), []).append(point)
    return {"now": now, "minutes": minutes, "nodes": result}


def scores(now, day):
    """The reading scores and room readings of today, or of every day ("past")."""
    today = time.strftime("%Y-%m-%d", time.localtime(now))
    since = int(time.mktime(time.strptime(today, "%Y-%m-%d"))) if day == "today" else 0
    score_rows = db_get(f"events?select=node_id,recorded_at,session,data&event_type=eq.score"
                        f"&recorded_at=gte.{since}&order=recorded_at")
    room_rows = db_get(f"events?select=recorded_at,data&event_type=eq.room&recorded_at=gte.{since}&order=recorded_at")
    room = []
    for row in room_rows:  # one room reading a minute is enough for the charts
        if not room or row["recorded_at"] - room[-1]["recorded_at"] >= 60:
            room.append({"recorded_at": row["recorded_at"], **row["data"]})
    lines = [{"node_id": row["node_id"], "recorded_at": row["recorded_at"], "session": row["session"], **row["data"]}
             for row in score_rows]
    return {"scores": lines, "room": room}


class handler(Handler):
    """Vercel runs this class: the web server from _plumbing.py, answering with the functions above."""
    routes = {"telemetry": telemetry, "assess": assess, "page": page, "word_audio": word_audio,
              "nodes": nodes, "history": history, "scores": scores}


if __name__ == "__main__":  # local test: python3 api/lumpi.py  ->  http://localhost:8081
    from http.server import HTTPServer
    HTTPServer(("0.0.0.0", 8081), handler).serve_forever()
