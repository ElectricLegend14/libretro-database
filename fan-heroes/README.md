# Emberfall Heroes

An offline fan game inspired by the tactics of mobile hero-collection RPGs. All characters, names, and art are original, and the game uses no assets from any commercial title.

## Play

Open `index.html` in any modern browser. It needs no install, server, or internet connection. Progress is saved in your browser's local storage.

## What's in it

- **Battles** on a 6×8 grid across three maps (Normal, Hard, Lunatic). You get player and enemy phases, a danger zone overlay, and an enemy AI that picks its best target.
- **Combat rules:** the red/blue/green weapon triangle (±20% Atk), double attacks at +5 Spd, range-based counters, bows that are effective against fliers, magic that targets Res, and staff healing.
- **Terrain:** forests slow infantry and block cavalry. Water and mountains can only be crossed by fliers. Walls block everyone.
- **Specials** charge each time a unit attacks or is attacked: Glimmer, Moonbow, Luna, Bonfire, Dragon Aura, and Astra.
- **Summoning:** five colored stones per session cost 5/4/4/4/3 orbs. Rarities are 3★, 4★, and 5★, and duplicate heroes merge for +1 to every stat (up to +10).
- **Allies:** build a team of four from your barracks.
- **Hero Forge:** design your own hero (name, weapon, movement type, special, and stats within a budget). Forged heroes join the summon pool.

## Customizing

All game data sits at the top of the `<script>` in `index.html`:

| Constant   | What it controls                                          |
|------------|-----------------------------------------------------------|
| `ROSTER`   | Summonable heroes and their 5★ stats                      |
| `FOES`     | Enemy unit templates                                      |
| `MAPS`     | Terrain rows (`.` plain, `f` forest, `m` mountain, `w` water, `#` wall), starting tiles, enemy placements, and rewards |
| `SPECIALS` | Special skills and cooldowns (damage math is in `hitDamage`) |
| `WEAPONS`, `MOVES` | Weapon ranges and types, movement types           |

To add a map, append an entry to `MAPS` with 8 terrain strings of 6 characters each.
