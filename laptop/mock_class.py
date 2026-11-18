"""Mock LUMPI class: sends the same messages the nest, the dolls and the laptop AI send.

Use it to try the dashboard without hardware. Every message goes through
POST /api/telemetry with the X-LUMPI-Token header, exactly like real hardware,
so nothing on the page is hard coded: another --seed gives other numbers.

    LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py                (terminal 1)
    LUMPI_LAN_KEY=test python3 laptop/mock_class.py --seed 7 --days 4    (terminal 2)

Sends --days past classes and today's class (50 min, ending now), then prints
the numbers the page should show, to compare by eye.
Past classes are saved to data/scores and data/room like real ones; delete
those folders to start clean.

Fixed data (same numbers every time):
    python3 laptop/mock_class.py --seed 7 --days 4 --save tests/fixtures/dashboard/mock_class.jsonl
    LUMPI_LAN_KEY=test python3 laptop/mock_class.py --load tests/fixtures/dashboard/mock_class.jsonl
The file holds one message per line, exactly the HTTP body. --load moves every
time by the same amount so today's class ends now. Start the server fresh and
delete data/scores and data/room first, or the scores are saved twice.
tests/test_mock_class.py checks every message against the server's format.
"""

import json
import os
import random
import sys
import time
import urllib.request

URL = "http://localhost:8080/api/telemetry"
CLASS_MINUTES = 50
TODAY_STEP_S = 10
PAST_STEP_S = 30
ATTEMPT_EVERY_S = 120

# Two books, two passages each. The laptop AI scores every word and every IPA sound.
PASSAGES = {
    "04A1B2C3": {1: "the cat sat on the mat", 2: "three fish think the ship is red"},
    "04D5E6F7": {1: "very light rain fell", 2: "she thought the ball rolled through the gate"},
}
IPA = {
    "the": ["ð", "ə"], "cat": ["k", "æ", "t"], "sat": ["s", "æ", "t"], "on": ["ɒ", "n"],
    "mat": ["m", "æ", "t"], "three": ["θ", "r", "iː"], "fish": ["f", "ɪ", "ʃ"],
    "think": ["θ", "ɪ", "ŋ", "k"], "ship": ["ʃ", "ɪ", "p"], "is": ["ɪ", "z"],
    "red": ["r", "e", "d"], "very": ["v", "e", "r", "i"], "light": ["l", "aɪ", "t"],
    "rain": ["r", "eɪ", "n"], "fell": ["f", "e", "l"], "she": ["ʃ", "iː"],
    "thought": ["θ", "ɔː", "t"], "ball": ["b", "ɔː", "l"], "rolled": ["r", "əʊ", "l", "d"],
    "through": ["θ", "r", "uː"], "gate": ["ɡ", "eɪ", "t"],
}
# Sounds Thai children often find hard score lower.
HARD = {"θ": -30, "ð": -25, "r": -18, "v": -12, "ʃ": -8, "z": -6}

READING_STEPS = ["book_on", "recording", "waiting_ai", "feedback"]


def clamp(value):
    return max(0, min(100, round(value)))


def mode_plan(rng, total_s):
    """Mode blocks for one doll: [(mode, seconds)], starting idle, mostly reading."""
    plan = [("idle", rng.randint(60, 240)), ("reading", rng.randint(6, 12) * 60), ("idle", rng.randint(30, 120))]
    used = sum(seconds for _, seconds in plan)
    while used < total_s:
        mode = rng.choice(["reading", "reading", "quiet"])
        seconds = rng.randint(6, 14) * 60 if mode == "reading" else rng.randint(4, 9) * 60
        plan.append((mode, seconds))
        used += seconds
        # Short idle between blocks, so each reading block is its own session.
        plan.append(("idle", rng.randint(30, 120)))
        used += plan[-1][1]
    return plan


def mode_at(plan, offset):
    """Mode at this many seconds into the class."""
    start = 0
    for mode, seconds in plan:
        if offset < start + seconds:
            return mode
        start += seconds
    return plan[-1][0]


def score_attempt(rng, skill, uid, passage):
    words = []
    for text in PASSAGES[uid][passage].split():
        phonemes = [{"ipa": sound, "score": clamp(skill + HARD.get(sound, 0) + rng.uniform(-10, 10))} for sound in IPA[text]]
        words.append({"word": text, "score": clamp(sum(p["score"] for p in phonemes) / len(phonemes)), "phonemes": phonemes})
    score = clamp(sum(w["score"] for w in words) / len(words))
    return {"uid": uid, "passage": passage, "score": score, "words": words}


def make_class(seed, days, now):
    """Every message of `days` past classes and today's class, in time order, plus expected numbers."""
    rng = random.Random(seed)
    events = []
    # Per doll: starting skill and how much it changes per class day.
    dolls = {1: (rng.uniform(50, 70), rng.uniform(1, 5)), 2: (rng.uniform(55, 75), rng.uniform(-3, 2))}
    expected = {"today": {"scores": [], "words": {}, "sounds": {}}}

    for days_ago in range(days, -1, -1):
        end = int(now) - days_ago * 86400
        start = end - CLASS_MINUTES * 60
        step = TODAY_STEP_S if days_ago == 0 else PAST_STEP_S
        day_events = []

        # Room: light level for the day, teacher walking around.
        lux_base = rng.choice([90, 250, 450, 700, 950]) + rng.uniform(-40, 40)
        distance = rng.uniform(80, 300)
        distance_at = {}
        for t in range(start, end + 1, step):
            distance = max(40, min(450, distance + rng.uniform(-40, 40)))
            distance_at[t] = distance
            lux = max(0, lux_base + rng.uniform(-20, 20))
            day_events.append(("nest", {"node_id": 0, "event_type": "room", "recorded_at": t, "activity": None,
                                        "data": {"lux": round(lux, 1), "distance_cm": round(distance, 1)}}))

        for doll_id, (base, trend) in dolls.items():
            plan = mode_plan(rng, end - start)
            skill = base + trend * (days - days_ago)
            uid = rng.choice(list(PASSAGES))
            passage = rng.choice([1, 2])
            attempt = 0
            last_attempt_t = 0
            asked_help = False
            previous_mode = None
            for t in range(start, end + 1, step):
                mode = mode_at(plan, t - start)
                if mode == "reading" and previous_mode != "reading":
                    uid = rng.choice(list(PASSAGES))
                    passage = rng.choice([1, 2])
                    attempt = 0
                    last_attempt_t = t
                    day_events.append(("nest", {"node_id": doll_id, "event_type": "start", "recorded_at": t, "activity": "book_on",
                                                "data": {"uid": uid, "passage": passage, "attempt": 1, "fsr": rng.randint(2500, 3500)}}))
                previous_mode = mode

                if mode == "reading":
                    activity = READING_STEPS[(t // step) % len(READING_STEPS)]
                    data = {"uid": uid, "passage": passage, "attempt": max(attempt, 1), "fsr": rng.randint(2500, 3500)}
                elif mode == "quiet":
                    activity = "quiet_study"
                    data = {"uid": None, "passage": None, "attempt": None, "fsr": 0}
                else:
                    activity = "idle"
                    data = {"uid": None, "passage": None, "attempt": None, "fsr": 0}
                day_events.append(("nest", {"node_id": doll_id, "event_type": "status", "recorded_at": t, "activity": activity, "data": data}))

                if mode == "reading" and t - last_attempt_t >= ATTEMPT_EVERY_S:
                    attempt += 1
                    last_attempt_t = t
                    # Light and a near teacher help a little; practising the same passage helps too.
                    bonus = (lux_base - 500) / 100 + (3 if distance_at[t] < 200 else 0) + 4 * min(attempt - 1, 3)
                    result = score_attempt(rng, skill + bonus, uid, passage)
                    result["attempt"] = attempt
                    day_events.append(("ai", {"node_id": doll_id, "event_type": "score", "recorded_at": t + 1, "activity": None,
                                              "data": {"uid": uid, "passage": passage, "attempt": attempt,
                                                       "score": result["score"], "words": result["words"]}}))
                    if days_ago == 0:
                        expected["today"]["scores"].append(result["score"])
                        for word in result["words"]:
                            expected["today"]["words"].setdefault(word["word"], []).append(word["score"])
                            for sound in word["phonemes"]:
                                expected["today"]["sounds"].setdefault(sound["ipa"], []).append(sound["score"])

                if mode == "quiet" and rng.random() < 0.08:
                    day_events.append(("nest", {"node_id": doll_id, "event_type": "noise", "recorded_at": t + 2, "activity": None,
                                                "data": {"db": round(rng.uniform(60, 80), 1)}}))

                # Past classes: one help press at a random time. Today: Doll 1 asks near the end, so the popup shows.
                if days_ago > 0 and not asked_help and mode == "reading" and rng.random() < 0.01:
                    asked_help = True
                    day_events.append(("nest", {"node_id": doll_id, "event_type": "help", "recorded_at": t + 3, "activity": None, "data": {}}))
            if days_ago == 0 and doll_id == 1:
                day_events.append(("nest", {"node_id": 1, "event_type": "help", "recorded_at": end - 20, "activity": None, "data": {}}))

        day_events.sort(key=lambda item: item[1]["recorded_at"])
        events.extend(day_events)

    # Event IDs: the nest's own counter and the laptop AI's counter.
    counters = {"nest": 0, "ai": 0}
    prefix = {"nest": f"mock_nest_{seed}", "ai": f"laptop_ai_{seed}"}
    result = []
    for source, event in events:
        counters[source] += 1
        result.append({"event_id": f"{prefix[source]}/{counters[source]}", **event})
    return result, expected


def load_events(path, now):
    """Messages from a --save file, every time moved so the last one is now."""
    with open(path, encoding="utf-8") as file:
        events = [json.loads(text) for text in file if text.strip()]
    shift = int(now) - max(event["recorded_at"] for event in events)
    for event in events:
        event["recorded_at"] += shift
    return events


def expected_from_events(events):
    """The same expected numbers as make_class, worked out from the messages (today = the last class)."""
    last = max(event["recorded_at"] for event in events)
    today = {"scores": [], "words": {}, "sounds": {}}
    for event in events:
        if event["event_type"] != "score" or event["recorded_at"] < last - CLASS_MINUTES * 60 - 60:
            continue
        today["scores"].append(event["data"]["score"])
        for word in event["data"]["words"]:
            today["words"].setdefault(word["word"], []).append(word["score"])
            for sound in word["phonemes"]:
                today["sounds"].setdefault(sound["ipa"], []).append(sound["score"])
    return {"today": today}


def average(values):
    return sum(values) / len(values)


def summary(expected):
    today = expected["today"]
    if not today["scores"]:
        return "No reading attempts today (try another --seed)."
    worst_sound = min(today["sounds"], key=lambda ipa: average(today["sounds"][ipa]))
    worst_word = min(today["words"], key=lambda word: average(today["words"][word]))
    return "\n".join([
        f"Today: {len(today['scores'])} attempts, class score {round(average(today['scores']))}%",
        f"Weakest sound: /{worst_sound}/ {round(average(today['sounds'][worst_sound]))}%",
        f"Weakest word: {worst_word} {round(average(today['words'][worst_word]))}%",
    ])


def send(event, token):
    body = json.dumps(event, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(URL, data=body, method="POST", headers={"X-LUMPI-Token": token, "Content-Type": "application/json"})
    with urllib.request.urlopen(request) as response:
        return json.loads(response.read())["result"]


def main():
    seed = int(time.time())
    days = 4
    arguments = sys.argv[1:]
    if "--seed" in arguments:
        seed = int(arguments[arguments.index("--seed") + 1])
    if "--days" in arguments:
        days = int(arguments[arguments.index("--days") + 1])

    if "--save" in arguments:
        path = arguments[arguments.index("--save") + 1]
        events, expected = make_class(seed, days, time.time())
        with open(path, "w", encoding="utf-8") as file:
            for event in events:
                file.write(json.dumps(event, ensure_ascii=False) + "\n")
        print(f"seed {seed}: saved {len(events)} messages to {path}")
        print(summary(expected))
        return

    token = os.environ.get("LUMPI_LAN_KEY", "")
    if not token:
        print("Set LUMPI_LAN_KEY first (same value as the server).")
        return

    if "--load" in arguments:
        path = arguments[arguments.index("--load") + 1]
        events = load_events(path, time.time())
        expected = expected_from_events(events)
        label = f"file {path}"
    else:
        events, expected = make_class(seed, days, time.time())
        label = f"seed {seed}"
    results = {}
    for event in events:
        result = send(event, token)
        results[result] = results.get(result, 0) + 1
    print(f"{label}: sent {len(events)} messages {results}")
    print(summary(expected))


if __name__ == "__main__":
    main()
