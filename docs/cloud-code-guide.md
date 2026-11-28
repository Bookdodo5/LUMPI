# Cloud code guide (read this before a teacher asks)

The cloud part of LUMPI: one Python program on **Vercel**, a database and file store on **Supabase**,
one speech model on **Runpod**. Standard library only, no frameworks.

## What to read, in order

| File | Read it? | What it does |
|---|---|---|
| `api/lumpi.py` | **Yes, all of it** | LUMPI's logic, top to bottom in story order (below) |
| `ai/app.py` | Yes | The AI on Runpod: recording -> the IPA sounds it hears. It does not grade |
| `tools/add_book.py` | Top part | On the laptop: a book's pages -> each word's sounds (IPA) + a spoken clip, uploaded |
| `api/_plumbing.py` | No | Web server, passwords, Supabase calls, size limits, safe file names |
| `vercel.json` | Glance | Which web address goes to `api/lumpi.py`; `/` shows `dashboard/v7` |
| `tests/test_cloud.py`, `tests/test_add_book.py` | If asked | Offline tests with a fake database and fake AI |

## The story in `api/lumpi.py`

1. **The nest sends an event** (`POST /api/telemetry`: room, start, status, help, noise).
   `telemetry()` stores it once: a resent `event_id` is a `duplicate`.
2. **The nest sends a recording** (`POST /v1/assess`). `assess()`: load the book -> the AI writes
   the sounds it heard -> `grade()` finds which part of the page was read and compares it with the
   book's sounds -> store a `score` event -> reply `code,word` (1 good, 2 practise that word, 3 nothing
   heard, 4 try again), then for codes 1 and 2 the `S`, `W` and `P` lines of `reply_lines()`:
   ```text
   S,14,3,72                                    first page word read, words read, score
   W,14,w,fish,f ɪ ʃ,p ɪ s,/v1/word_audio?...   page word, r/w, word, book sounds, heard ("-" missed), clip
   P,ɪ,r                                        each book sound read: r right, w wrong somewhere
   ```
   - `words_read()` finds the stretch of the page that was read (the child may start anywhere).
   - `norm()` makes both sides spell a sound the same way (British base, accents, stress marks).
   - `align()` lines the two sound lists up with the fewest changes (Levenshtein distance).
   - `score_words()`: a word's score = sounds right / its sounds (extra sounds count against it).
3. **A word's clip and a book's title** (`GET /v1/word_audio`, `GET /v1/book`) for the nest.
4. **The dashboard** (`GET /api/dashboard/nodes|history|scores`): rebuilt from the stored events on
   every request: latest state per doll (online/stale/offline, help), recent messages, scores.

## The teacher dashboard (`dashboard/v7`, plain HTML + JavaScript, no build step)

- `index.html` + `app.js`: Overview (doll rows, room, mode bars, activity, score trend, sounds, words).
- `doll.html` + `doll.js`: one doll (header chips, today's timeline, meter, summary, "did practising help").
- `session.html` + `session.js`: one doll's latest reading session, or the whole class, word by word.
- `shared.js`: what all three use: `repeat()` polling, `watchNodes()` (nest dot, help popup),
  `watchScores()` (Today / Past buttons), score maths (`sessionsOf`, `wordsOf`, `soundsOf`), lists and charts.
- `style.css`: every style; the HTML only holds structure and ids that the JavaScript fills.
- The pages only read `/api/dashboard/*`; the server works out online/stale/offline and `needs_help`.
- Full guided tour of the dashboard code, for beginners: [dashboard-guide.md](dashboard-guide.md).

## What the plumbing guarantees (so the logic need not)

- Only the nest (LAN key) can send; only the teacher (dashboard password) can read the dashboard.
- Bodies have a size limit; a UID or word must match a strict pattern before it becomes a file name.
- `/v1/assess` always answers within 14 s (the nest waits 18 s); recordings up to 10 s; any failure answers `4,0` ("try again").
- Database down on `/api/telemetry` = `503`, so the nest keeps the event and resends it.
- An event without the six fields of the right types is refused (`400`) so it cannot break the dashboard.

Test: `python3 -m unittest tests.test_cloud tests.test_add_book` (no internet needed).
