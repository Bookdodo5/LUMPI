"""Turn per-page OCR text (data/books/ocr/book<N>/<pdfpage>.tsv) into book JSON.

    python3 -I tools/ocr_to_book.py 1 "/path/book 1.pdf"     # -> data/books/book1.full.json
    python3 -I tools/ocr_to_book.py test                     # self-check, no files needed

Output has add_book.py's shape, keyed by PRINTED page number. book<N>.full.json is the page as printed;
book<N>.upload.json is the same with numbers as words and no symbols, ready for tools/add_book.py.
The OCR files come from: pdftoppm -r 300 -gray -f N -l N book.pdf | tesseract stdin <NNN> txt tsv
Also writes book<N>.review.txt: words where this OCR and the PDF's own text layer disagree.
"""
import difflib, json, re, subprocess, sys, unicodedata
from collections import Counter
from pathlib import Path

DATA = Path(__file__).resolve().parent.parent / "data" / "books"
# offset: PDF page - printed page. first/last: PDF pages of the numbered body.
# head: the running header printed at the top of a page. fixes: {printed page: [(wrong, right)]}, checked by eye.
BOOKS = {
    "1": {"uid": "00000001", "title": "Minecraft: The Island", "offset": 16, "first": 19, "last": 286,
          "head": r"^\W*(MAX\s+BR\w+|MIN\w+:?\s+THE\s+ISLAND)\W*(\d+)?$",
          "fixes": {
                    "4": "U choked>I choked",
                    "7": "iiber>über|1s>is",
                    "14": "finch>flinch",
                    "16": "degnemaiely>desperately",
                    "23": "sec>see|Rir>Rrrr",
                    "24": "im>in",
                    "25": "ofa>of a|halfremembered>half-remembered",
                    "38": "toa>to a",
                    "50": "Grubhh>Gruhhh",
                    "55": "g’moring>g’morning",
                    "57": "switled>swirled",
                    "60": "selfrepaired>self-repaired",
                    "72": "v2>",
                    "74": "Gn>Grrr|ignite>igniter",
                    "83": "ina>in a",
                    "91": "it’cause>it ’cause",
                    "111": "tertible>terrible",
                    "120": "Wap>Whp",
                    "126": "ata>at a",
                    "136": "asa>as a",
                    "141": "Ill>I’ll",
                    "159": "emyueeling>exasperating|Abhhh>Ahhhh",
                    "162": "outall>out all",
                    "164": "RWARE>AWARE",
                    "167": "Grubhh>Gruhhh",
                    "196": "HOT>NOT|FRILURE>FAILURE",
                    "198": "fery>fiery",
                    "208": "butimportant>but-important",
                    "217": "F could>I could",
                    "220": "tumed>turned",
                    "224": "Ill>I’ll",
                    "233": "celcbration>celebration",
                    "243": "Gm>Grrr",
                    "244": "ELOOM>BLOOM",
                    "245": "Gubhh>Guhhh",
                    "247": "Useghhh>Uggghhh",
                    "250": "nota>not a|itll>it’ll",
                    "251": "Gubhh>Guhhh",
                    "257": "tumed>turned",
                    "264": "Ewerybody>Everybody|itll>it’ll|outa>out a"}},
    "2": {"uid": "00000002", "title": "Satan, Cantor, and Infinity", "offset": 11, "first": 12, "last": 281,
          "head": r"^(.{0,10}SATAN\W+CANTOR.*|[A-Z“‘\"'][^.:;]{2,50}?\s+\d{1,3})$", "fixes": {}},
}
WORD = re.compile(r"[A-Za-z]+(?:[’'][A-Za-z]+)*")
FOLIO = re.compile(r"^\W{0,3}(\d{1,3}|[ivxl]{1,6})\W{0,3}$")


MIN_CONF = 40  # a line whose words average less than this is a picture or ornament, not text (raise to drop more)
# Italic "I" and its contractions, as tesseract misreads them in these two scans.
READ_AS_I = [(r"(?<!\w)\|(?!\w)", "I"), (r"\|(?=[’'])", "I"), (r"\b(Tm|Pm|lm)\b", "I’m"), (r"\bTt(?=[’'])", "It"),
             (r"\b(Id|Pd)\b", "I’d"), (r"\bIJ\b", "I"),
             (r"(?<!CHAPTER )(?<![\w.,-])1(?= [a-z])|(?<=\.\.\.)1(?= [a-z])", "I"),
             (r'(?<=[“"*])T(?=[ ’\'])', "I"), (r'([“"])[“"]*[*]+', r"\1"), (r"“{2,}", "“"),
             (r"\s*(?<!\w)e ?\d{1,3} ?e(?!\w)\s*", "\n"),  # section ornament "• 3 •"
             (r"(?<!\bcaf)é", "e"), (r"\bGedel", "Gödel"), (r"\bWirH A TWINGE", "With a twinge"), (r"(?<![\w’'-])d(?= [a-z])", "I’d")]


def tsv_text(tsv):
    """Tesseract's TSV -> text, one line per printed line, '' between paragraphs, junk lines left out."""
    lines = {}
    for row in tsv.splitlines()[1:]:
        c = row.split("\t")
        speck = c[0] == "5" and float(c[10]) < MIN_CONF and not re.search(r"[A-Za-z0-9|\[\]{]", c[11])
        if c[0] == "5" and c[11].strip() and not speck:  # level 5 = one word
            lines.setdefault(tuple(c[2:5]), []).append((float(c[10]), c[11]))
    out, last = [], None
    for key, words in lines.items():
        if sum(conf for conf, _ in words) / len(words) < MIN_CONF:
            continue
        if last and key[:2] != last:
            out.append("")
        out.append(" ".join(w for _, w in words))
        last = key[:2]
    return "\n".join(out)


def fix_i(text):
    for wrong, right in READ_AS_I:
        text = re.sub(wrong, right, text)
    if "]" not in text:  # a "[" with no partner is an "I"
        text = re.sub(r"\[(?=[a-z])", "I ", text).replace("[", "I")
    if "[" not in text:
        text = re.sub(r"(?<=\b[Ii]f)\]|(?<![\w])[\]{](?=[ a-z])", lambda m: " I" if m.string[m.start() - 1:m.start()] == "f" else "I", text)
    return re.sub(r"\bI(?=(could|shouted|began|jumped|shattered|made|need|just|hadn)\b)", "I ", text)


def page_lines(text, head):
    """Lines of one page without the running header, page number and scan specks ('' = paragraph gap)."""
    lines = [re.sub(r"^[_|~.\-—]\s+", "", l.strip()) for l in text.replace("\f", "").splitlines()]
    lines = [l for l in lines if not l or re.search(r"[A-Za-z0-9]", l)]
    for end in (0, -1):  # top, then bottom
        while lines and (not lines[end] or FOLIO.match(lines[end]) or (end == 0 and re.match(head, lines[end]))):
            lines.pop(end)
    return lines


def join_lines(lines, vocab):
    """Wrapped lines -> paragraphs separated by newlines; words split by a hyphen are rejoined."""
    out = ""
    for line in lines:
        if not line:
            out = out.rstrip(" ") + "\n" if out and not out.endswith(("-", "¬")) else out
            continue
        if out.endswith(("-", "¬")):
            a = re.search(r"([A-Za-z]*)[-¬]$", out).group(1).lower()
            b = (WORD.match(line) or re.match("", "")).group(0).lower()
            keep = not vocab[a + b] and vocab[a] > 1 and vocab[b] > 1  # "tension-draining", not "re-member"
            out = (out if keep else out[:-1]) + line
        elif out and not out.endswith(("\n", "—")):
            out += " " + line
        else:
            out += line
    return out.strip()


def cross_check(text, other, vocab):
    """Compare with the PDF's own text layer. Take its word only where ours is unknown and theirs is common."""
    ours, theirs = list(WORD.finditer(text)), WORD.findall(other)
    sm = difflib.SequenceMatcher(None, [m.group().lower() for m in ours], [w.lower() for w in theirs], autojunk=False)
    changes, doubts = [], []
    for tag, i, i2, j, j2 in sm.get_opcodes():
        if tag != "replace":
            continue
        if i2 - i == j2 - j:
            for m, w in zip(ours[i:i2], theirs[j:j2]):
                old = m.group()
                if old.replace("’", "'") == w.replace("’", "'"):  # only the apostrophe's shape differs
                    continue
                lone = old in ("J", "T") and w == "I"  # italic or drop-quote "I" read as J / T
                if lone or (vocab[old.lower()] < 2 and vocab[w.lower()] >= 5 and len(old) > 1):
                    changes.append((m.start(), m.end(), w))
                    doubts.append(f"FIXED {old!r} -> {w!r}")
                elif vocab[old.lower()] < 3 or vocab[w.lower()] > 20 * vocab[old.lower()]:
                    doubts.append(f"?     {old!r} / {w!r}")
        elif any(vocab[m.group().lower()] < 2 for m in ours[i:i2]):
            doubts.append(f"?     {' '.join(m.group() for m in ours[i:i2])!r} / {' '.join(theirs[j:j2])!r}")
    for start, end, w in reversed(changes):
        text = text[:start] + w + text[end:]
    return text, doubts


ONES = ("zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
        "fifteen sixteen seventeen eighteen nineteen").split()
TENS = "twenty thirty forty fifty sixty seventy eighty ninety".split()


def in_words(n):
    """0..9999 as British English words."""
    if n < 20:
        return ONES[n]
    if n < 100:
        return TENS[n // 10 - 2] + ("-" + ONES[n % 10] if n % 10 else "")
    size, name = (1000, "thousand") if n >= 1000 else (100, "hundred")
    rest = n % size
    return f"{in_words(n // size)} {name}" + ((" and " if rest < 100 else " ") + in_words(rest) if rest else "")


def speakable(text):
    """Page text with only what add_book.py accepts: numbers written as words, accents and symbols removed."""
    text = re.sub(r"\d+", lambda m: " " + (in_words(int(m[0])) if len(m[0]) < 5 else " ".join(ONES[int(c)] for c in m[0])) + " ",
                  text.replace("%", " percent").replace("…", "..."))
    text = "".join(c for c in unicodedata.normalize("NFKD", text) if not unicodedata.combining(c))
    text = re.sub(r"[^A-Za-z \n’'‘.,!?;:\"()\-–—“”]", " ", text)
    return re.sub(r" *\n *", "\n", re.sub(r" {2,}| (?=[-.,!?;:])", lambda m: " " if len(m[0]) > 1 else "", text)).strip()


def build(n, pdf):
    b = BOOKS[n]
    layer = subprocess.run(["pdftotext", "-layout", pdf, "-"], capture_output=True, check=True, text=True).stdout.split("\f")
    files = {int(f.stem): tsv_text(f.read_text("utf-8")) for f in (DATA / "ocr" / f"book{n}").glob("*.tsv")}
    missing = [p for p in range(b["first"], b["last"] + 1) if p not in files]
    if missing:
        sys.exit(f"OCR text missing for PDF pages {missing[:10]}...")
    # how often each word is seen, in both readings: our only dictionary
    vocab = Counter(w.lower() for t in list(files.values()) + layer for w in WORD.findall(t))

    # Check the page offset against every page number the OCR did read.
    seen = Counter()
    for p in range(b["first"], b["last"] + 1):
        l = [x.strip() for x in files[p].splitlines() if x.strip()]
        found = {int(m) for x in l[:1] + l[-1:] for m in re.findall(r"^\d{1,3}\b|\b\d{1,3}$", x)}
        if found:
            seen["match" if p - b["offset"] in found else "differ"] += 1
    print(f"page numbers read on {sum(seen.values())} pages: {dict(seen)}")

    pages, review = {}, []
    for p in range(b["first"], b["last"] + 1):
        text = join_lines(page_lines(files[p], b["head"]), vocab)
        if len(WORD.findall(text)) < 3:  # blank or picture-only page
            continue
        number = str(p - b["offset"])
        text = fix_i(text)
        text, doubts = cross_check(text, join_lines(page_lines(layer[p - 1], b["head"]), vocab), vocab)
        for pair in filter(None, b["fixes"].get(number, "").split("|")):
            wrong, right = pair.split(">")  # whole words only
            text, hits = re.subn(rf"(?<![\w’]){re.escape(wrong)}(?!\w)", right, text)
            assert hits, f"page {number}: fix {wrong!r} no longer matches"
        pages[number] = text
        review += [f"page {number} (pdf {p}): {d}" for d in doubts]

    # A word cut by the page turn goes wholly onto the page where it starts.
    keys = list(pages)
    for a, z in zip(keys, keys[1:]):
        if int(z) == int(a) + 1 and re.search(r"[A-Za-z][-¬]$", pages[a]):
            rest = re.match(r"[A-Za-z’']+\S*\s*", pages[z])
            if rest:
                pages[a] = join_lines([pages[a], rest.group().strip()], vocab)
                pages[z] = pages[z][rest.end():]

    book = {"uid": b["uid"], "title": b["title"], "language": "en-gb", "pages": pages}
    (DATA / f"book{n}.full.json").write_text(json.dumps(book, ensure_ascii=False, indent=1), "utf-8")
    spoken = {k: speakable(t) for k, t in pages.items()}
    upload = dict(book, pages={k: t for k, t in spoken.items() if WORD.search(t)})
    (DATA / f"book{n}.upload.json").write_text(json.dumps(upload, ensure_ascii=False, indent=1), "utf-8")
    (DATA / f"book{n}.review.txt").write_text("\n".join(review) + "\n", "utf-8")
    words = [len(WORD.findall(t)) for t in pages.values()]
    print(f"book{n}: {len(pages)} pages ({keys[0]}-{keys[-1]}), {sum(words)} words, median {sorted(words)[len(words)//2]}; "
          f"auto-fixed {sum('FIXED' in r for r in review)}, to review {sum('FIXED' not in r for r in review)}")


def demo():
    head = BOOKS["1"]["head"]
    lines = page_lines("MAX BROOKS\n\nI couldn’t re-\nmember a tension-\ndraining day.\n\n_ Yes.\n\n20\n", head)
    vocab = Counter({"remember": 3, "tension": 2, "draining": 2})
    text = join_lines(lines, vocab)
    assert text == "I couldn’t remember a tension-draining day.\nYes.", repr(text)
    assert page_lines("The Lie Detective 9\nHe lied.", BOOKS["2"]["head"]) == ["He lied."]
    fixed, _ = cross_check("J must go", "I must go", Counter({"i": 9, "j": 1, "must": 9, "go": 9}))
    assert fixed == "I must go", fixed
    assert fix_i("| said, “What am [?” [shouted. Tm done; d know.") == "I said, “What am I?” I shouted. I’m done; I’d know."
    assert fix_i("If] go, ] will; {could.") == "If I go, I will; I could.", fix_i("If] go, ] will; {could.")
    assert tsv_text("h\n" + "\n".join(f"5\t1\t1\t1\t{l}\t1\t0\t0\t0\t0\t{c}\t{w}" for l, c, w in [(1, 95, "Hi"), (2, 9, "eee")])) == "Hi"
    assert fix_i("““*T see,’ he said. e 6 e “*No, présence at a café") == "“I see,’ he said.\n“No, presence at a café"
    assert speakable("A 360-degree turn, déjà vu = 99% [1]") == "A three hundred and sixty-degree turn, deja vu ninety-nine percent one", speakable("A 360-degree turn, déjà vu = 99% [1]")
    print("ok")


if __name__ == "__main__":
    demo() if sys.argv[1:] == ["test"] else build(*sys.argv[1:3])
