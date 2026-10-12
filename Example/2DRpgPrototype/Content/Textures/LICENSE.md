# Texture sources

All Kenney packs are CC0 1.0 (public domain dedication): free for personal,
educational and commercial use; crediting Kenney is appreciated, not required.
https://kenney.nl

| File | Source | License |
| --- | --- | --- |
| `tiny_town.png` | Kenney "Tiny Town" (`tilemap_packed.png`, 16px tiles, 12×11), https://kenney.nl/assets/tiny-town | CC0 1.0 |
| `tiny_dungeon.png` | Kenney "Tiny Dungeon" (`tilemap_packed.png`, 16px tiles, 12×11), https://kenney.nl/assets/tiny-dungeon | CC0 1.0 |
| `tiny_battle.png` | Kenney "Tiny Battle" (`tilemap_packed.png`, 16px tiles, 18×11), https://kenney.nl/assets/tiny-battle | CC0 1.0 |
| `grass.png` | Tile 0 of `tiny_town.png`, cropped | CC0 1.0 |
| `hero_walk.png` | Generated for this project with an image model, then reduced to 16×24 cells by `Tools/quantize_walk_sheet.py --cell-height 24` (3 columns: left foot, stand, right foot × 4 rows: down, left, right, up; the right row is the left row mirrored) | Project asset |
| `Particles/*.png` | Kenney "Particle Pack" (`PNG (Transparent)`), https://kenney.nl/assets/particle-pack | CC0 1.0 |

The Tiny Dungeon atlas is the main tileset for this game: floors, walls, doors,
chests, characters (indices 84–95), monsters (108–131), weapons and potions, all
on a 16px grid with no padding. Cell values in scene files are `tile + 1` (0 =
empty); see `Content/Tilesets/dungeon.yatileset.json` for the solid list.
