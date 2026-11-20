"""Build the Windows Block Lens Modtopia package with Recompia's bundled Zig."""

import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tomllib
import zipfile


HERE = Path(__file__).resolve().parent
OUTPUT = HERE / "build"
SDK = HERE / "vendor/modtopia"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="copy the package to Recompia's mods folder")
    parser.add_argument("--game-dir", type=Path, default=HERE.parent.parent,
                        help="Recompia folder containing tools/build/zig and mods")
    parser.add_argument("--zig", type=Path, help="path to a Zig executable")
    args = parser.parse_args()
    game = args.game_dir.resolve()
    zig = (args.zig or game / "tools/build/zig/zig.exe").resolve()
    if not zig.is_file():
        parser.error(f"Zig not found at {zig}")

    OUTPUT.mkdir(exist_ok=True)
    env = dict(os.environ,
               ZIG_GLOBAL_CACHE_DIR=str(OUTPUT / "zig-global"),
               ZIG_LOCAL_CACHE_DIR=str(OUTPUT / "zig-local"))
    dll = OUTPUT / "mod.dll"
    subprocess.run(
        [zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-s",
         "-Wall", "-Wextra", "-Werror", "-I", SDK / "sdk/include", "-shared",
         "mod.c", "block_target.c", "block_names.c", "block_status.c", "overlay.c",
         "-luser32", "-lgdi32", "-lole32", "-luuid", "-lwindowscodecs", "-lmsimg32", "-o", dll],
        cwd=HERE, env=env, check=True,
    )

    manifest = HERE / "mod.toml"
    metadata = tomllib.loads(manifest.read_text(encoding="utf-8"))
    name = f"{metadata['id']}-{metadata['version']}.pkmod"
    package = OUTPUT / name
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(manifest, "mod.toml")
        archive.write(dll, "bin/windows-x64/mod.dll")
        archive.write(HERE / "README.md", "README.md")
        archive.write(HERE / "LICENSE", "licenses/Block-Lens-LICENSE")
        archive.write(SDK / "LICENSE", "licenses/Modtopia-LICENSE")
    print(f"Built {package}")
    if args.install:
        destination = game / "mods" / name
        staged = destination.with_suffix(".tmp")
        shutil.copyfile(package, staged)
        staged.replace(destination)
        print(f"Installed {destination}")


if __name__ == "__main__":
    main()
