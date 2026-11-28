// Reading session page (session.html).
//   /session.html          the whole class today: every passage read, each word = class average.
//   /session.html?doll=2   that doll's latest session today: every try, word by word.
// Scores come from the cloud grader (grade() in api/lumpi.py), one per reading attempt.

let dollId = Number(new URLSearchParams(location.search).get("doll"));
if (!DOLL_IDS.includes(dollId)) {
  dollId = null;  // the whole class
}

// Back button: the doll page for one doll, the overview for the class.
if (dollId === null) {
  document.title = "LUMPI · Class passages";
} else {
  document.title = `LUMPI · Doll ${dollId} reading session`;
  byId("back-link").href = `/doll.html?doll=${dollId}`;
  setText("back-link", `‹ Doll ${dollId}`);
}

// The sounds of a word in reading order, each averaged over every read of that word: [{ipa, avg}].
// reads = scores of the same word, e.g. [{word, score, phonemes}].
function soundsInOrder(reads) {
  return reads[0].phonemes.map((sound, index) => {
    const same = reads.map((read) => read.phonemes[index])
      .filter((item) => item !== undefined && item.ipa === sound.ipa);
    return { ipa: sound.ipa, avg: mean(same, (item) => item.score) };
  });
}

// One word cell: the word, its %, then every sound of the word marked right (✓) or wrong (✗,
// under the Practise line), so the teacher sees which sound made the word wrong.
function wordCell(text, score, sounds, under) {
  const strip = el("div", { class: "sounds" }, ...sounds.map((sound) => {
    const right = sound.avg >= SCORE_PRACTISE;
    const tooltip = `/${sound.ipa}/ ${right ? "right" : "wrong"}`;
    return el("div", { class: right ? "sound" : "sound wrong", title: tooltip },
      el("div", {}, sound.ipa), el("div", {}, right ? "✓" : "✗"));
  }));
  const cell = el("div", { class: "cell" },
    el("div", { class: `w ${band(score).css}`, title: `${text} · ${percent(score)}` }, text),
    el("div", { class: score < SCORE_PRACTISE ? "pc low" : "pc" }, percent(score)),
    strip);
  if (under) {
    cell.append(el("div", { class: "tm" }, under));
  }
  return cell;
}

// One row: a label on the left, word cells after it.
function passageRow(label, cells) {
  return el("div", { class: "passage-row" },
    el("div", { class: "passage-label" }, label),
    el("div", { class: "passage-words" }, ...cells));
}

// Fills the page: header line, meter, Good / Close / Practise word counts, timing card, passage
// rows and the lowest words and sounds. page = the texts and numbers of drawDoll() or drawClass().
function drawPage(page) {
  setText("session-info", page.info);
  setText("score-label", page.scoreLabel);
  byId("score-gauge").replaceChildren(scoreGauge(page.avg));
  setText("score-text", page.scoreText);
  const words = page.lines.flatMap((line) => line.words);
  const counts = BANDS.map((item) => words.filter((word) => band(word.score) === item).length);
  byId("band-bar").replaceChildren(...BANDS.map((item, i) => {
    return el("div", { style: `flex:${counts[i]};background:${item.color}` });
  }));
  byId("band-text").replaceChildren(...BANDS.map((item, i) => el("span", { style: `color:${item.color}` },
    `● ${item.name} · ${counts[i]}`)));
  setText("timing-title", page.timingTitle);
  byId("timing").replaceChildren(...page.timing.map(([label, value]) => statCell(label, value)));
  setText("passage-title", page.passageTitle);
  setText("passage-note", page.passageNote);
  byId("passages").replaceChildren(...page.rows);
  byId("word-rows").replaceChildren(...wordRows(wordsOf(page.lines), 8));
  byId("sound-rows").replaceChildren(...soundRows(soundsOf(page.lines), 8));
}

// One doll: its latest session today, every try.
function drawDoll(scores) {
  const sessions = sessionsOf(ofDoll(scores, dollId));
  const session = sessions[sessions.length - 1];
  const previous = sessions[sessions.length - 2];
  const lines = session.lines;
  const rows = lines.map((line) => passageRow(
    `p.${line.passage} · try ${line.attempt}\n${clockTime(line.recorded_at)} · ${percent(line.score)}`,
    line.words.map((word) => wordCell(word.word, word.score, soundsInOrder([word]), ""))));
  const passages = new Set(lines.map((line) => `${line.uid}/${line.passage}`)).size;
  drawPage({
    info: `Doll ${dollId} · book ${lines[0].uid} · ${clockTime(session.start)}`,
    scoreLabel: `Reading score · session ${clockTime(session.start)}`,
    avg: session.avg,
    scoreText: previous === undefined ? `${session.count} tries`
      : `${changeText(session.avg - previous.avg)} pts vs previous session (${percent(previous.avg)})`,
    lines: lines,
    timingTitle: "Timing",
    timing: [["Started", clockTime(session.start)], ["Total", minutesText(session.end - session.start)],
             ["Tries", String(session.count)], ["Passages", String(passages)]],
    passageTitle: "Passage, word by word · every try",
    passageNote: "Word %, then each IPA sound: ✓ right · ✗ wrong",
    rows: rows,
  });
}

// The whole class: per book and passage, every page word anyone read, in page order. A try may read
// only part of the page: first_word is the page word it began at. Each word is averaged over its reads.
function drawClass(scores) {
  const dolls = new Set(scores.map((line) => line.node_id)).size;
  const sessions = new Set(scores.map((line) => `${line.node_id}/${line.session}`)).size;
  const rows = groupBy(scores, (line) => `${line.uid}/${line.passage}`).map((group) => {
    const reads = new Map();  // page word number -> every read of that word
    for (const line of group.items) {
      line.words.forEach((word, index) => {
        const place = (line.first_word || 0) + index;
        reads.set(place, [...(reads.get(place) || []), word]);
      });
    }
    const cells = [...reads.keys()].sort((a, b) => a - b).map((place) => {
      const same = reads.get(place);
      return wordCell(same[0].word, mean(same, (item) => item.score), soundsInOrder(same), `${same.length} reads`);
    });
    const latest = group.items[group.items.length - 1];
    const readers = new Set(group.items.map((line) => line.node_id)).size;
    const label = `Book ${latest.uid} · p.${latest.passage}\n${group.items.length} tries · ${readers} dolls`;
    return passageRow(label, cells);
  });
  drawPage({
    info: `Whole class · today · ${dolls} dolls`,
    scoreLabel: "Class reading score · today",
    avg: average(scores),
    scoreText: `${scores.length} tries from ${dolls} dolls`,
    lines: scores,
    timingTitle: "Class today",
    timing: [["Dolls", String(dolls)], ["Sessions", String(sessions)], ["Tries", String(scores.length)],
             ["Passages", String(rows.length)]],
    passageTitle: "Passages, word by word · whole class",
    passageNote: "Class word %, then each IPA sound: ✓ right · ✗ wrong (class average)",
    rows: rows,
  });
}

watchNodes(() => {}, () => {});  // only for the help popup

watchScores((data) => {
  const scores = dollId === null ? data.scores : ofDoll(data.scores, dollId);
  if (scores.length > 0) {
    (dollId === null ? drawClass : drawDoll)(scores);
    return;
  }
  setText("session-info", "No reading sessions yet today");
  byId("score-gauge").replaceChildren(scoreGauge(null));
  const note = "No reading sessions yet today. They appear once the AI sends scores.";
  byId("passages").replaceChildren(emptyNote(note));
  byId("word-rows").replaceChildren();
  byId("sound-rows").replaceChildren();
}, () => setText("session-info", "Cloud not reachable · retrying"));
