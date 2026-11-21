# JADE for Pokopia

JADE for Pokopia shows a small nameplate for the block you are looking at. Point the camera at a nearby block, or point at it directly in the game's mouse mode. The label uses the game's language. Enable **Show unnamed targets by ID** in the Mods settings to inspect scenery with no recognized name.

JADE was previously called Block Lens. The package still uses the `block_lens` mod ID so existing settings carry over.

Camera aim looks through seawater to the block behind it. Mouse mode hides the tooltip when the game reports seawater as the pointer hit.

- **F7** shows the target's terrain or object ID and tile coordinates while held.
- **F8** toggles the tooltip for this session.
- The Mods settings let you change both keys, pick a tooltip position, and hide item icons. Key changes take effect after restarting.
- Item icons use the local PokoHex export in `pokohex_data/images/items`. The tooltip shows the name by itself when the corresponding image is unavailable.
- Move hints cover common terrain, tall grass, dry crops, and ripe crops. Recognized crops show sprout, growing, or ready to harvest. The bar tracks these visible growth stages.

Block damage and breaking progress are not available in this release.

## Play

JADE runs on Windows x64 with Recompia 0.15.2 and Pokémon Pokopia 2.0.1a (Modtopia 0.7). Use borderless or windowed display mode.

1. Close the game and download the `.pkmod` from the [latest GitHub release](https://github.com/imhlq/pokopia-block-lens/releases/latest).
2. Put it in Recompia's `mods` folder. If you're updating, remove the previous `block_lens-*.pkmod` first.
3. Start the game from `PokopiaLauncher.exe` and allow the code mod if prompted.

## Build

From the source folder, run this with Python 3.11 or newer.

```powershell
python build.py --game-dir "C:\path\to\Recompia"
```

The script uses Recompia's bundled Zig and writes the package to `build/`. You can also pass `--zig` with the path to a standalone Zig executable. If the source is in `mod_sources/block_lens` under your Recompia installation, omit `--game-dir`.

To verify the numeric name indexes against your own Pokopia 2.0.1a dump, run `python build_names.py --check --romfs "C:\path\to\RomFS"`. The build uses the checked-in index and doesn't need the game dump.

## License

JADE for Pokopia and the bundled Modtopia SDK are MIT-licensed. The SDK header is pinned to [Recompia v0.13.9](https://github.com/Dittobyte/Recompia/tree/8e79edb2f3649348ac095295ee2375000188d4e7/runtime/third_party/modtopia).
