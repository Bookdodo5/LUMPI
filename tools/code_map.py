"""Every function of LUMPI's real code, as JSON for the build guide's "Understand the code" pages.

    python3 tools/code_map.py ../lumpi_build_guide/src/data/code-map.json

Reads the COMMITTED code (git HEAD), so every GitHub link points at the right line.
For each function: part, file, line, kind, the comment above it (or its docstring), its source,
and which other LUMPI functions it calls. Also the FreeRTOS tasks, the nest's routes and settings.
"""
import json
import re
import subprocess
import sys

REPO = "https://github.com/Bookdodo5/LUMPI/blob"
PARTS = [  # (part, folder or file prefix): first match wins
    ("doll", "src/app/station/"), ("nest", "src/app/gateway/"), ("rules", "src/common/"),
    ("drivers", "src/drivers/"), ("cloud", "api/"), ("ai", "ai/"), ("dashboard", "dashboard/v7/"),
    ("tools", "tools/"),
]
SKIP = {"src/drivers/check_tools.h", "tools/code_map.py", "tools/pio_doll_data.py"}
NOT_NAMES = {"if", "for", "while", "switch", "return", "sizeof", "catch", "defined"}


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def comment_above(lines, i, mark):
    """The comment lines right above line i (C++/JS "//", Python "#")."""
    out = []
    while i > 0 and lines[i - 1].strip().startswith(mark):
        i -= 1
        out.insert(0, lines[i].strip()[len(mark):].strip())
    return " ".join(out)


def brace_end(lines, i):
    """The line where the { ... } block that starts on or after line i closes."""
    depth, seen = 0, False
    for j in range(i, len(lines)):
        code = re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'|//.*', "", lines[j])
        depth += code.count("{") - code.count("}")
        seen = seen or "{" in code
        if seen and depth <= 0:
            return j
    return len(lines) - 1


C_DEF = re.compile(r"^(\s*)(?:(?:static|inline|constexpr|const)\s+)*(?!return\b|else\b|case\b|new\b)[\w:<>*&]+[\s*&]+(?:IRAM_ATTR\s+)?\**(\w+)\s*\((?:[^;]*|.*\{.*\}\s*)$")
C_LAMBDA = re.compile(r"^\s*auto (\w+) = \[")
JS_DEF = re.compile(r"^(\s*)(?:async\s+)?function (\w+)\s*\(")
PY_DEF = re.compile(r"^(\s*)def (\w+)\s*\(")


def functions_in(path, text):
    lines = text.split("\n")
    found = []
    if path.endswith(".py"):
        for i, line in enumerate(lines):
            m = PY_DEF.match(line)
            if not m:
                continue
            indent = len(m.group(1))
            end = i + 1
            while end < len(lines) and (not lines[end].strip() or len(lines[end]) - len(lines[end].lstrip()) > indent):
                end += 1
            body = "\n".join(lines[i:end]).rstrip()
            doc = re.search(r'"""(.*?)(?:\n\s*\n|""")', body, re.S)
            summary = " ".join(doc.group(1).split()) if doc else comment_above(lines, i, "#")
            found.append((m.group(2), i, i + len(body.split("\n")) - 1, summary, "function"))
        return found, lines
    js = path.endswith(".js")
    i = 0
    while i < len(lines):
        line = lines[i]
        m = (JS_DEF if js else C_DEF).match(line)
        lam = None if js else C_LAMBDA.match(line)
        if m and m.group(2) not in NOT_NAMES and not line.rstrip().endswith(";"):
            end = brace_end(lines, i)
            kind = "interrupt" if "IRAM_ATTR" in line else "task" if m.group(2).endswith("_task") else "function"
            kind = "method" if kind == "function" and m.group(1) and not js else kind  # inside a struct
            found.append((m.group(2), i, end, comment_above(lines, i, "//"), kind))
            if js:
                i = end
        elif lam:
            found.append((lam.group(1), i, brace_end(lines, i), comment_above(lines, i, "//"), "lambda"))
        i += 1
    return found, lines


def main(out_path):
    sha = git("rev-parse", "HEAD").strip()
    files, functions, sources = [], [], {}
    for path in git("ls-files").split("\n"):
        part = next((p for p, prefix in PARTS if path.startswith(prefix)), None)
        if not part or path in SKIP or not re.search(r"\.(h|cpp|py|js)$", path):
            continue
        text = sources[path] = git("show", f"HEAD:{path}")
        found, lines = functions_in(path, text)
        files.append({"path": path, "part": part, "lines": len(lines), "functions": len(found),
                      "about": first_comment(lines)})
        for name, start, end, summary, kind in found:
            summary = "" if summary.startswith("static") else summary  # a linker note, not what it does
            same = sum(f["file"] == path and f["name"] == name for f in functions)  # an overload: name#2
            functions.append({"id": f"{path}#{name}" + (f"#{same + 1}" if same else ""), "name": name, "part": part, "file": path, "line": start + 1,
                              "end": end + 1, "kind": kind, "summary": summary,
                              "source": "\n".join(lines[start:end + 1])})
    order = [p for p, _ in PARTS]
    functions.sort(key=lambda f: (order.index(f["part"]), f["file"], f["line"]))
    link_calls(functions, sources)
    data = {"sha": sha, "repo": f"{REPO}/{sha}", "files": files, "functions": functions,
            "tasks": tasks(), "routes": routes(), "settings": settings()}
    with open(out_path, "w", encoding="utf-8") as out:
        json.dump(data, out, ensure_ascii=False, indent=0)
    print(f"{len(functions)} functions in {len(files)} files, commit {sha[:7]} -> {out_path}")


def first_comment(lines):
    """A file's opening comment: its first comment block, without the leading marks."""
    out = []
    for line in lines[:12]:
        s = line.strip()
        if s.startswith(("//", "#")) and not s.startswith(("#include", "#pragma", "#!")):
            out.append(s.lstrip("/#").strip())
        elif s.startswith('"""'):
            out.append(s.strip('"'))
        elif out:
            break
    first = " ".join(out)
    return re.split(r"(?<=\.)\s", first, maxsplit=1)[0][:200]  # the first sentence is enough to recognise a file


def lang(path):
    return path.rsplit(".", 1)[-1].replace("cpp", "h")


def link_calls(functions, sources):
    """calls / called_by between LUMPI's own functions, found by reading each body:
    name(...)          a call (same file first; a name in one other file only)
    ns::name(...)      a call into that namespace's file
    obj.name(...)      a call of a struct method (C++), e.g. mistakes.apply(...)
    name, / name)      a function passed by name (xTaskCreatePinnedToCore, attachInterrupt, callbacks)
    routes["name"]     the cloud's web handler calling the story function"""
    by_name, in_namespace = {}, {}
    for f in functions:
        by_name.setdefault(f["name"], []).append(f)
    for path, text in sources.items():
        for ns in re.findall(r"^namespace (\w+)", text, re.M):
            in_namespace.setdefault(ns, set()).add(path)
    for f in functions:
        py = f["file"].endswith(".py")
        head = re.search(r"\)\s*(?:->[^:]*)?:\s*\n" if py else r"\)\s*(?:const\s*)?\{", f["source"])
        signature, body = (f["source"][:head.end()], f["source"][head.end():]) if head else (f["source"], "")
        params = set(re.findall(r"\w+", signature))
        targets = [g for name in re.findall(r'routes\["(\w+)"\]', body) for g in by_name.get(name, []) if g["file"] == "api/lumpi.py"]
        body = re.sub(r'"(\\.|[^"\\\n])*"|\'(\\.|[^\'\\\n])*\'|//.*|#.*', "", body)
        for name in set(re.findall(r"\b(\w+)\b", body)) & set(by_name):
            pool = [g for g in by_name[name] if lang(g["file"]) == lang(f["file"]) and g["id"] != f["id"]]
            free = [g for g in pool if g["kind"] != "method"]
            for ns in re.findall(rf"(\w+)::{name}\s*\(", body):
                targets += [g for g in free if g["file"] in in_namespace.get(ns, ())]
            called = re.search(rf"(?:(?<![.\w:>])|(?<=self\.)){name}\s*\(", body)
            passed = ("_" in name and name not in params and re.search(rf"(?<![.\w]){name}\s*[,)]", body)) or \
                re.search(rf"[?:]\s*{name}\s*[):]|\b{name}\s*\)\s*\(", body)  # also (a ? f : g)(x)
            if called or passed:
                same = [g for g in free if g["file"] == f["file"]]
                if same or len({g["file"] for g in free}) == 1:
                    targets += same or free  # several = overloads of one function: link them all
            members = [g for g in pool if g["kind"] == "method"]
            if not py and members and len({g["file"] for g in members}) == 1 and re.search(rf"(?:\.|->){name}\s*\(", body):
                targets += members
        f["calls"] = sorted({g["id"] for g in targets})
    for f in functions:
        f["called_by"] = [g["id"] for g in functions if f["id"] in g["calls"]]


def tasks():
    """FreeRTOS tasks from each xTaskCreatePinnedToCore line: name, stack, priority, core."""
    out = []
    for path, part in [("src/app/station/doll.cpp", "doll"), ("src/app/gateway/nest.cpp", "nest")]:
        for fn, name, stack, prio, core in re.findall(
                r'xTaskCreatePinnedToCore\((\w+), "(\w+)", (\d+), nullptr, (\d+), [^,]+, (\d)\)', git("show", f"HEAD:{path}")):
            out.append({"part": part, "function": f"{path}#{fn}", "name": name, "stack": int(stack),
                        "priority": int(prio), "core": int(core)})
    return out


def routes():
    """Doll -> nest routes and nest -> cloud routes (messages.h), each with the function that answers it."""
    messages = git("show", "HEAD:src/common/messages.h")
    nest = git("show", "HEAD:src/app/gateway/nest.cpp")
    cloud_route = dict(re.findall(r'"source": "(/[^"]+)", "destination": "/api/lumpi\?route=([:\w]+)"', git("show", "HEAD:vercel.json")))
    handler = dict(re.findall(r"path == route::(\w+)\)\s+return (\w+)\(", nest), status="on_status")
    out = []
    for space, between in [("route", "doll -> nest"), ("cloud_route", "nest -> cloud")]:
        block = re.search(rf"namespace {space} \{{(.*?)\}}", messages, re.S).group(1)
        for name, path, about in re.findall(r'constexpr const char\* (\w+) = "(/v1/\w+)";\s*// (.*)', block):
            answer = (f"src/app/gateway/nest.cpp#{handler[name]}" if between == "doll -> nest"
                      else f"api/lumpi.py#{cloud_route[path]}")
            out.append({"path": path, "between": between, "about": about.strip(), "handler": answer})
    out.append({"path": "/v1/word_audio", "between": "nest -> cloud", "handler": "api/lumpi.py#word_audio",
                "about": "GET ?uid&passage&word -> one word's spoken clip (WAV) | 404; the address comes in a W line"})
    out.append({"path": "/api/telemetry", "between": "nest -> cloud", "handler": "api/lumpi.py#telemetry",
                "about": "POST one dashboard event (JSON) -> added | duplicate"})
    for name in ["nodes", "history", "scores"]:
        out.append({"path": f"/api/dashboard/{name}", "between": "dashboard -> cloud", "handler": f"api/lumpi.py#{name}",
                    "about": "GET, teacher password -> JSON for the dashboard pages"})
    return out


def settings():
    """Every pin and tuning constant in config/settings.h, with its comment."""
    out, space = [], ""
    for line in git("show", "HEAD:config/settings.h").split("\n"):
        ns = re.match(r"namespace (\w+)", line)
        space = ns.group(1) if ns else space
        m = re.match(r"\s*constexpr\s+[^=;]*?\b(\w+)\s*=\s*([^;]+);\s*(?://\s*(.*))?", line)
        if m:
            out.append({"group": space, "name": m.group(1), "value": m.group(2).strip(), "about": (m.group(3) or "").strip()})
    return out


if __name__ == "__main__":
    main(sys.argv[1])
