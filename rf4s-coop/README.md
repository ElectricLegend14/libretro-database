# rf4s-coop

An unofficial two-player co-op mod for **Rune Factory 4 Special** (PC/Steam), in progress.
It isn't affiliated with Marvelous or XSEED. You need your own copy of the game, and this
project never ships game files.

The design, phases and protocol are in the design doc:
<https://claude.ai/artifact/9xrnbA5kp8nq2CztPtjbTZ>

## Status

| Phase | What | State |
| --- | --- | --- |
| 0 · Groundwork | Loader, signature scanner, find player / map / clock in memory | Code done; **the game's signatures and offsets are still unknown** |
| 1 · Ghost player | Two games see each other move | Networking and sync done and tested against a simulated game; drawing the partner in-world needs RE target 4 |
| 2 · Companion | Guest fights, uses tools, follows between maps | Leash logic done; combat not started |
| 3 · Full co-op | Farm sync, drop-in, guest save, Steam invites | Not started |

So today the mod loads into the game, logs what it can find and stays switched off
until `src/win/layout.hpp` is filled in. Nothing is written to game memory until then.

## Layout

```
include/coop/   portable core: protocol, UDP, sessions, signatures, config
src/            core implementation + SimGame (a fake game for tests)
src/win/        the mod DLL: game adapter, layout.hpp (signatures/offsets), DllMain
tools/          coop_sim (try host/guest without the game), sigscan, injector
tests/          unit and loopback session tests
```

The sync code only talks to the game through `IGameAdapter`, so it runs and is tested
on any OS with `SimGame` standing in for RF4S.

## Build

Portable core, tools and tests (Linux, macOS or Windows):

```sh
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

The Windows DLL and injector, cross-compiled from Linux with MinGW-w64
(`apt install g++-mingw-w64-x86-64`):

```sh
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

On Windows, Visual Studio or MinGW work directly with the first command.

## Try the networking without the game

```sh
./build/coop_sim host 7777
./build/coop_sim guest 127.0.0.1 7777      # in a second terminal
```

Each side prints where it sees its partner. Stop one side to watch the other time
out and then reconnect.

## Use it with the game (Windows)

1. Put `rf4s-coop.dll`, `rf4s-coop-inject.exe` and an `rf4s-coop.ini` in one folder.
2. Start the game.
3. Run `rf4s-coop-inject.exe "<game exe name>"` (the name shown in Task Manager's Details tab).
4. Read `rf4s-coop.log` in the same folder.

`rf4s-coop.ini`:

```ini
role = host            # or guest
port = 7777
host_address = 1.2.3.4 # guest only
name = Lest
```

Both players need the same game version. The log prints its build ID, which the host
checks when the guest joins. `coop_sim --build <id>` can join a real host for testing.

## Helping with reverse engineering

Every unknown lives in `src/win/layout.hpp`. For each target in the design doc's
reverse-engineering plan:

1. Find it with Cheat Engine / x64dbg, then find an instruction in the game's code that
   loads it through a RIP-relative address (`mov rax, [rip+...]`).
2. Copy enough bytes around that instruction to make a pattern, with `??` over the
   displacement.
3. Check it: `sigscan "<game>.exe" "48 8B 05 ?? ?? ?? ?? ..."` must say *Unique match*.
4. Add it and any structure offsets to `layout.hpp`, rebuild and inject. The log shows
   whether it resolved, and once the player is found it logs the live position every
   second (phase 0's gate).

All reads and writes go through `ReadProcessMemory` / `WriteProcessMemory` on the
game's own process, so a wrong offset gives you wrong numbers in the log rather than a crash.

## Safety

- Back up your save folder before trying the mod.
- The guest's character will be stored in its own file, never in the game's save slots.
- The mod refuses to start a session on a game build it doesn't recognise.
