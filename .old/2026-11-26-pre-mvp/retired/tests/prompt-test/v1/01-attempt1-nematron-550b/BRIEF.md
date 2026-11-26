# Task

Rebuild the "LUMPI Teacher Dashboard" using ONLY the specification in
`prompt.md` (in this folder) and the reference screenshots in
`dashboard-prompt-images/`. Then capture screenshots of your result with
Playwright.

# Where

- Your working folder is the current directory (`~/work`). Create every file
  inside it. Use the file layout described in prompt.md section 4.2
  (`dashboard/`, `laptop/`, `tests/fixtures/dashboard/`, `tests/`).
- There is no existing code anywhere. Do not look for it. Write it yourself
  from prompt.md.
- Do not invoke any external coding-agent CLI (claude, codex, hermes).
  Implement it yourself.

# Constraints

- Follow prompt.md exactly (no build step, plain HTML/CSS/JS, Python standard
  library server, no chart library).
- Port: other runs share this machine, so set `PORT = 8201` in your server
  instead of 8080, and use http://localhost:8201 everywhere. Use the key
  `LUMPI_LAN_KEY=test`.
- Write the scenario files yourself (section 12), enough to reach every state
  in section 11.
- Stop every server process you start before you finish.

# Playwright

Node and Playwright are installed. Use them from a script, e.g.
`node capture.js` with `const { chromium } = require("playwright");`
(NODE_PATH is already set; browsers are already installed; do not run
npm install or playwright install).

Save screenshots in `result-screenshots/`, at 1280x900 (and 390 wide for the
phone shot), one per state in prompt.md section 11, named like the reference
ones (`01_unknown_empty.png`, `02_online_full.png`, ... `11_mobile_390.png`).
To reach stale/offline without long waits you may feed events with past
receive times into your server, but the page must show the real server data.

# Done means

1. `python3 -m unittest discover -s tests` passes (write the tests from section 12).
2. All 11 state screenshots exist in `result-screenshots/`.
3. Write `REPORT.md` (under 40 lines): files created, test output summary,
   which states were captured, anything from prompt.md you could not do or
   found unclear.
