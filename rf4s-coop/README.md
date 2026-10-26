# rf4s-coop

An unofficial two-player co-op mod for **Rune Factory 4 Special** (PC/Steam), in progress.
It isn't affiliated with Marvelous or XSEED. You need your own copy of the game, and this
project never ships game files.

The design, phases and protocol are in the design doc:
<https://claude.ai/artifact/9xrnbA5kp8nq2CztPtjbTZ>

## Status

The network and sync side of every phase is built and tested against a simulated game.
What's missing is the game side: finding RF4S's data and functions in memory. Until
`src/win/layout.hpp` is filled in, the mod loads into the game, logs what it can find
and stays switched off. Nothing is written to game memory until then.

| Phase | Sync and network (tested with `SimGame`) | Game side (needs reverse engineering) |
| --- | --- | --- |
| 0 · Groundwork | Loader, injector, signature scanner, config, save backups | Player, map and clock signatures |
| 1 · Ghost player | Movement at 20 Hz with interpolation, clock sync, handshake, timeouts, rejoin | Drawing the partner in the world (RE target 4) |
| 2 · Companion | Monsters, combat, damage to the guest, fainting, drops, map leash | Entity list, damage and spawn functions (RE targets 4, 5, 8) |
| 3 · Full co-op | Farm sync, drop-in/drop-out, desync repair, guest character file | Farm tiles, inventory, character data |
| 4 · Polish | | Steam invites (Steamworks), festivals, split-screen |

## How it works

- **Host-authoritative.** The host's game is the real world. The guest runs their own
  copy of RF4S with its world stood down (`setLocalWorldSuppressed`) and shows the
  host's monsters, drops and farm instead. Everything the guest does to the world
  (attacks, pick-ups, farming) is a request the host checks and settles with the
  game's own rules.
- **Two channels over UDP.** State that's resent every tick anyway (positions, the
  clock, monster movement) goes unreliably. Events (spawns, hits, pick-ups, farm
  changes, chat) go over a reliable, ordered channel with acks and resends
  (`connection.hpp`).
- **Smoothing.** The partner and every monster are drawn 0.1 s in the past and
  blended between snapshots, so 20 updates a second look fluid.
- **Self-repair.** Every 2 s the host sends a fingerprint of what it has told the
  guest. If the guest's copy disagrees twice in a row, it throws its copy away and
  the host sends everything again.
- **Save safety.** Saves are backed up before each session. The guest's character
  is kept in its own file (`guest-character.bin`, atomic writes, CRC-checked, with a
  `.bak`), never in the game's save slots.

## Layout

```
include/coop/   portable core: protocol, connection, sessions, interpolation,
                checksums, storage, signatures, config, IGameAdapter
src/            core implementation + SimGame (a fake game for tests and coop_sim)
src/win/        the mod DLL: RF4S adapter, frame hook (MinHook + thunk), default layout, DllMain
tools/          coop_sim (host/guest without the game), sigscan, injector
tests/          unit tests and full host/guest sessions over a simulated network
```

The sync code only talks to the game through `IGameAdapter`, so it runs and is tested
on any OS with `SimGame` standing in for RF4S. Each adapter method beyond the basics
has a do-nothing default, so the real adapter can grow one found piece at a time.

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

Each side prints where it sees its partner. The host's world has three monsters near
the guest's path: the guest swings at whatever is in reach, gets bitten, and picks up
what drops. Stop one side to watch the other time out and then reconnect.

The tests also run full sessions on a simulated network with lag, jitter, packet loss
and duplicates (`ConditionedTransport`).

## Use it with the game (Windows)

1. Put `rf4s-coop.dll`, `rf4s-coop-inject.exe` and an `rf4s-coop.ini` in one folder.
   (The Windows CI job uploads a ready-built DLL and tools as the `rf4s-coop-windows`
   artifact.)
2. Start the game.
3. Run `rf4s-coop-inject.exe "<game exe name>"` (the name shown in Task Manager's Details tab).
4. Read `rf4s-coop.log` in the same folder.

`rf4s-coop.ini`:

```ini
role = host            # or guest
port = 7777
host_address = 1.2.3.4 # guest only
name = Lest
save_dir = C:\path\to\RF4S\saves   # backed up to save-backups\ before each session
backup_keep = 5        # 0 turns backups off
```

Both players need the same game version. The log prints its build ID, which the host
checks when the guest joins. `coop_sim --build <id>` can join a real host for testing.

## Helping with reverse engineering

Every unknown is a signature or an offset. While hunting, put guesses in
`rf4s-layout.ini` next to the DLL (start from `rf4s-layout.example.ini`): it overrides
the compiled-in values at startup, so trying a guess needs no rebuild. For each target
in the design doc's reverse-engineering plan:

1. Find it with Cheat Engine / x64dbg, then find an instruction in the game's code that
   loads it through a RIP-relative address (`mov rax, [rip+...]`).
2. Copy enough bytes around that instruction to make a pattern, with `??` over the
   displacement.
3. Check it: `sigscan "<game>.exe" "48 8B 05 ?? ?? ?? ?? ..."` must say *Unique match*.
4. Add it to `rf4s-layout.ini` and inject. The log shows whether it resolved, and once
   the player is found it logs the live position every second (phase 0's gate).
5. To find offsets inside the player object, set `dump_player_bytes = 256`: the log
   gets a hex dump every 2 s. Walk, take damage or spend RP between dumps and look
   for the bytes that change.
6. Find the game's per-frame update function and give it a `frame.signature`. With it,
   co-op runs inside that function on the game's own thread (the log says so). That's
   required before the mod writes to game memory. The hook is a small assembly thunk
   that forwards any signature untouched; it was tested with float arguments, a float
   return value and stack arguments.
7. Once a value is confirmed, copy it into `src/win/layout.hpp` so it ships in the DLL.

All reads and writes go through `ReadProcessMemory` / `WriteProcessMemory` on the
game's own process, so a wrong offset gives you wrong numbers in the log rather than a crash.

## Safety

- Back up your save folder yourself before trying the mod, and set `save_dir` so the
  mod does it too before every session.
- The guest's character is stored in its own file, never in the game's save slots.
- The mod refuses to start a session when the two players' game builds differ.
- Every packet is length- and range-checked before use, and the host checks every
  guest request against its own world. The tests fuzz the decoder with 300,000
  random and mutated packets and flood a live host with garbage.
- Plain UDP isn't authenticated or encrypted: someone who can forge packets from
  your partner's exact address could disrupt a session (not crash it). Play with
  people you trust over LAN or a VPN for now; internet play is meant to move to
  Steam Networking Sockets, which encrypts and authenticates.

## CI

`.github/workflows/rf4s-coop.yml` builds and tests on Linux (with AddressSanitizer
and UndefinedBehaviorSanitizer) and on Windows with MSVC, cross-compiles with MinGW,
and uploads the Windows DLL and tools as a build artifact.
