// Reading session page.
//   /session.html          whole class today: every passage read, each word = class average.
//   /session.html?doll=2   that doll's latest session today: every try, word by word.
// Scores come from the cloud grader (api/lumpi.py grade()), one per reading attempt.

const SCORE_POLL_MS = 10000;
const WORDS_SHOWN = 8;
const SOUNDS_SHOWN = 8;

let dollId = Number(new URLSearchParams(location.search).get("doll"));
if (!DOLL_IDS.includes(dollId)) {
  dollId = null;
}

// Back button: the doll page for one doll, the overview for the class.
const back = document.getElementById("back-link");
if (dollId !== null) {
  document.title = `LUMPI · Doll ${dollId} reading session`;
  back.href = `/doll.html?doll=${dollId}`;
  back.textContent = `‹ Doll ${dollId}`;
} else {
  document.title = "LUMPI · Class passages";
}

// The sounds of a word in reading order, each averaged over every read of that word:
// [{ipa, avg}]. reads = word scores of the same word, e.g. [{word, score, phonemes}].
function soundsInOrder(reads) {
  return reads[0].phonemes.map((sound, index) => {
    const same = reads.map((read) => read.phonemes[index]).filter((item) => item !== undefined && item.ipa === sound.ipa);
    return { ipa: sound.ipa, avg: same.reduce((sum, item) => sum + item.score, 0) / same.length };
  });
}

// One word cell: the word, its %, then every IPA sound of the word marked
// right or wrong, so the teacher sees which sound made the word wrong.
function wordCell(text, score, sounds, under) {
  const cell = document.createElement("div");
  cell.className = "cell";
  const word = document.createElement("div");
  word.className = `w ${bandClass(score)}`;
  word.textContent = text;
  word.title = `${text} · ${Math.round(score)}%`;
  const percent = document.createElement("div");
  percent.className = score < SCORE_PRACTISE ? "pc b" : "pc";
  percent.textContent = `${Math.round(score)}%`;
  cell.append(word, percent);

  // Each sound: right (green ✓) or wrong (red ✗). Wrong = under the Practise line.
  const strip = document.createElement("div");
  strip.style.cssText = "display:flex;justify-content:center;gap:2px;margin-top:3px";
  for (const sound of sounds) {
    const right = sound.avg >= SCORE_PRACTISE;
    const box = document.createElement("div");
    box.title = `/${sound.ipa}/ ${right ? "right" : "wrong"}`;
    box.style.cssText = `min-width:22px;padding:1px 3px;border-radius:4px;line-height:1.15;background:${right ? "color-mix(in srgb,var(--jp-ok) 15%,var(--jp-white))" : "var(--jp-red)"};color:${right ? "var(--jp-ok)" : "var(--jp-white)"}`;
    const ipa = document.createElement("div");
    ipa.style.cssText = "font-size:12px;font-weight:700";
    ipa.textContent = sound.ipa;
    const mark = document.createElement("div");
    mark.style.cssText = "font-size:10.5px;font-weight:700";
    mark.textContent = right ? "✓" : "✗";
    box.append(ipa, mark);
    strip.append(box);
  }
  cell.append(strip);

  if (under) {
    const small = document.createElement("div");
    small.className = "tm";
    small.textContent = under;
    cell.append(small);
  }
  return cell;
}

// One row: a label on the left, word cells after it.
function passageRow(label, cells) {
  const row = document.createElement("div");
  row.style.cssText = "display:grid;grid-template-columns:150px minmax(0,1fr);gap:10px;align-items:start;padding:10px 0;border-bottom:var(--jp-border)";
  const name = document.createElement("div");
  name.style.cssText = "font-size:12.5px;color:var(--jp-grey);padding-top:6px;white-space:pre-line";
  name.textContent = label;
  const words = document.createElement("div");
  words.style.cssText = "display:flex;flex-wrap:wrap;gap:8px 6px";
  words.append(...cells);
  row.append(name, words);
  return row;
}

function timingCell(label, value) {
  const cell = document.createElement("div");
  const small = document.createElement("div");
  small.style.cssText = "font-size:10.5px;text-transform:uppercase;letter-spacing:.12em;color:var(--jp-grey)";
  small.textContent = label;
  const big = document.createElement("div");
  big.style.cssText = "font-family:var(--jp-font-display);font-size:20px;font-weight:700";
  big.textContent = value;
  cell.append(small, big);
  return cell;
}

// Bar and counts of Good / Close / Practise words.
function drawBands(lines) {
  const counts = { g: 0, c: 0, p: 0 };
  for (const line of lines) {
    for (const word of line.words) {
      counts[bandClass(word.score)] += 1;
    }
  }
  const colors = { g: "var(--jp-ok)", c: "var(--jp-warn)", p: "var(--jp-red)" };
  const names = { g: "Good", c: "Close", p: "Practise" };
  const bars = [];
  const texts = [];
  for (const key of ["g", "c", "p"]) {
    const bar = document.createElement("div");
    bar.style.cssText = `flex:${counts[key]};background:${colors[key]};border-radius:var(--jp-radius)`;
    bars.push(bar);
    const text = document.createElement("span");
    text.textContent = `● ${names[key]} · ${counts[key]}`;
    text.style.color = colors[key];
    texts.push(text);
  }
  document.getElementById("band-bar").replaceChildren(...bars);
  document.getElementById("band-text").replaceChildren(...texts);
}

function drawDoll(scores) {
  const sessions = sessionsOf(scores, dollId);
  if (sessions.length === 0) {
    return false;
  }
  const session = sessions[sessions.length - 1];
  const lines = session.lines;
  const first = lines[0];

  document.getElementById("session-info").textContent = `Doll ${dollId} · book ${first.uid} · ${clockTime(session.start)}`;
  document.getElementById("score-label").textContent = `Reading score · session ${clockTime(session.start)}`;
  document.getElementById("score-gauge").replaceChildren(scoreGauge(session.avg));
  document.getElementById("score-text").textContent = sessions.length > 1 ? `${changeText(session.avg - sessions[sessions.length - 2].avg)} pts vs previous session (${Math.round(sessions[sessions.length - 2].avg)}%)` : `${session.count} tries`;
  drawBands(lines);

  const passages = new Set(lines.map((line) => `${line.uid}/${line.passage}`));
  document.getElementById("timing-title").textContent = "Timing";
  document.getElementById("timing").replaceChildren(
    timingCell("Started", clockTime(session.start)),
    timingCell("Total", minutesText(session.end - session.start)),
    timingCell("Tries", String(session.count)),
    timingCell("Passages", String(passages.size)),
  );

  document.getElementById("passage-title").textContent = "Passage, word by word · every try";
  document.getElementById("passage-note").textContent = "Word %, then each IPA sound: ✓ right · ✗ wrong";
  const rows = lines.map((line) => passageRow(
    `p.${line.passage} · try ${line.attempt}\n${clockTime(line.recorded_at)} · ${Math.round(line.score)}%`,
    line.words.map((word) => wordCell(word.word, word.score, soundsInOrder([word]), "")),
  ));
  document.getElementById("passages").replaceChildren(...rows);
  document.getElementById("word-rows").replaceChildren(...wordRows(wordsOf(lines, null), WORDS_SHOWN));
  document.getElementById("sound-rows").replaceChildren(...soundRows(soundsOf(lines, null), SOUNDS_SHOWN));
  return true;
}

function drawClass(scores) {
  if (scores.length === 0) {
    return false;
  }
  const avg = average(scores);
  const dolls = new Set(scores.map((line) => line.node_id));
  const sessions = new Set(scores.map((line) => `${line.node_id}/${line.session}`));

  // Group by book + passage. Word order comes from the latest try; each
  // position is averaged over every try that read the same word there.
  const byPassage = {};
  for (const line of scores) {
    const key = `${line.uid}/${line.passage}`;
    if (byPassage[key] === undefined) {
      byPassage[key] = { uid: line.uid, passage: line.passage, lines: [] };
    }
    byPassage[key].lines.push(line);
  }

  const rows = [];
  for (const group of Object.values(byPassage)) {
    const latest = group.lines[group.lines.length - 1];
    const cells = latest.words.map((word, index) => {
      const same = group.lines.map((line) => line.words[index]).filter((item) => item !== undefined && item.word.toLowerCase() === word.word.toLowerCase());
      const score = same.reduce((sum, item) => sum + item.score, 0) / same.length;
      return wordCell(word.word, score, soundsInOrder(same), `${same.length} reads`);
    });
    const readers = new Set(group.lines.map((line) => line.node_id));
    rows.push(passageRow(`Book ${group.uid} · p.${group.passage}\n${group.lines.length} tries · ${readers.size} dolls`, cells));
  }

  document.getElementById("session-info").textContent = `Whole class · today · ${dolls.size} dolls`;
  document.getElementById("score-label").textContent = "Class reading score · today";
  document.getElementById("score-gauge").replaceChildren(scoreGauge(avg));
  document.getElementById("score-text").textContent = `${scores.length} tries from ${dolls.size} dolls`;
  drawBands(scores);
  document.getElementById("timing-title").textContent = "Class today";
  document.getElementById("timing").replaceChildren(
    timingCell("Dolls", String(dolls.size)),
    timingCell("Sessions", String(sessions.size)),
    timingCell("Tries", String(scores.length)),
    timingCell("Passages", String(rows.length)),
  );
  document.getElementById("passage-title").textContent = "Passages, word by word · whole class";
  document.getElementById("passage-note").textContent = "Class word %, then each IPA sound: ✓ right · ✗ wrong (class average)";
  document.getElementById("passages").replaceChildren(...rows);
  document.getElementById("word-rows").replaceChildren(...wordRows(wordsOf(scores, null), WORDS_SHOWN));
  document.getElementById("sound-rows").replaceChildren(...soundRows(soundsOf(scores, null), SOUNDS_SHOWN));
  return true;
}

async function pollScores() {
  try {
    const data = await getJson("/api/dashboard/scores?day=today");
    const drawn = dollId === null ? drawClass(data.scores) : drawDoll(scoresOf(data.scores, dollId));
    if (!drawn) {
      document.getElementById("session-info").textContent = "No reading sessions yet today";
      document.getElementById("score-gauge").replaceChildren(scoreGauge(null));
      document.getElementById("passages").replaceChildren(emptyNote("No reading sessions yet today. They appear once the AI sends scores."));
      document.getElementById("word-rows").replaceChildren();
      document.getElementById("sound-rows").replaceChildren();
    }
  } catch (error) {
    console.warn("session poll failed:", error);
    document.getElementById("session-info").textContent = "Laptop server not reachable · retrying";
  }
  setTimeout(pollScores, SCORE_POLL_MS);
}

pollScores();
