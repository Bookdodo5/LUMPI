# LUMPI

A reading buddy puppet: two reading stations, one puppet gateway, a laptop speech AI and a cloud log.
CEDT Embedded Systems Final Project 2026.

**Build guide (read this first):** https://lumpi-build-guide.vercel.app

This repository starts empty on purpose. Every file is written by the team, following the guide.

## First steps (everyone)

1. Clone the repository and open the folder in VS Code with the PlatformIO extension.
2. Copy `config/private.example.h` to `config/private.h` and fill it in. Integration sends the real values privately. **Never commit `private.h`.**
3. Do the [Foundation](https://lumpi-build-guide.vercel.app/foundation/project) together: it creates `platformio.ini`, `partitions.csv`, the settings and the shared drivers.
4. Then follow your own track.

## Folders and owners

| Folder | What goes in it | Owner | Guide |
| --- | --- | --- | --- |
| `platformio.ini`, `partitions.csv` | Build settings for every board | Everyone (Foundation) | [Project files](https://lumpi-build-guide.vercel.app/foundation/project) |
| `config/` | `settings.h` (pins, timings) and your private `private.h` | Everyone / Integration | [Settings](https://lumpi-build-guide.vercel.app/foundation/settings) |
| `src/drivers/` | Shared helpers and sensor drivers | Everyone (Foundation) | [Sensor drivers](https://lumpi-build-guide.vercel.app/foundation/sensors) |
| `src/common/` | Code shared by stations and gateway (the referee) | Gateway & Cloud | [The referee](https://lumpi-build-guide.vercel.app/gateway/rules) |
| `src/bench/` | One small test program per part | Station, Gateway | [Station track](https://lumpi-build-guide.vercel.app/station/), [Gateway track](https://lumpi-build-guide.vercel.app/gateway/) |
| `src/app/station/` | Station firmware | Sensor Node | [Station firmware](https://lumpi-build-guide.vercel.app/station/firmware) |
| `src/app/gateway/` | Gateway firmware | Gateway & Cloud | [Plan and startup](https://lumpi-build-guide.vercel.app/gateway/plan) |
| `src/test/` | Rules tests run on the laptop | Test Lead | [Rules tests](https://lumpi-build-guide.vercel.app/testing/rules-tests) |
| `adapter/` | Laptop server, reading AI, books, Discord, cloud worker | Integration, Dashboard | [Integration track](https://lumpi-build-guide.vercel.app/integration/) |
| `dashboard/` | The live dashboard page | Dashboard | [Laptop server and dashboard](https://lumpi-build-guide.vercel.app/dashboard/server) |
| `tools/` | Helper scripts (IPA, audio checks, gateway watcher) | Integration | [Books, IPA and clips](https://lumpi-build-guide.vercel.app/integration/books) |
| `tests/` | Python tests for the laptop code | Test Lead | [Test plan](https://lumpi-build-guide.vercel.app/testing/test-plan) |
| `data/audio/` | The puppet's 4 voice clips (included) | Dashboard | [Commands and downloads](https://lumpi-build-guide.vercel.app/reference/commands) |
| `data/word_audio/` | One clip per book word | Dashboard | [Books, IPA and clips](https://lumpi-build-guide.vercel.app/integration/books) |

The empty `.gitkeep` files only make Git keep the empty folders. Delete one once its folder has real files.

## Working together

- `git pull` before you start. Commit small, and push when a test passes.
- Commit your own files under your own name: the course checks each member's history.
- Announce in the team chat before editing shared files (`settings.h`, `platformio.ini`, `gateway.h`).
- Secrets live only in `config/private.h`, which `.gitignore` blocks. The Discord webhook stays in an environment variable.
