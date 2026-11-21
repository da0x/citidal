# citadel

A studio for putting your own pictures into games: posters, signs, billboards, screens,
paintings and menu backgrounds. Pick a surface, drop a photo on it, place it, build a pack,
and press Play to see it in the game.

citadel never edits a game's own files. It builds a separate pack that the game loads
ahead of its own art, so taking the pack out puts everything back.

## Games

| Game | State |
|---|---|
| Half-Life 2, Episode One, Episode Two | working |
| Portal | working |
| Portal 2 | working |
| Quake III Arena | working |

Each engine lives in its own folder under `source/engines/`, behind the interface in
`source/engine/game.hpp`. An engine knows how its games store art, where they are
installed, how a pack is built and installed, and how the game is started. The rest of
citadel (pictures, placement, packs, the studio) knows nothing about any one game.

## Platforms

Linux now. Windows is next: the platform code is in `source/platform/linux/`, and a
`source/platform/windows/` folder with the same file names will sit beside it.

## Building

Needs CMake 3.28 or later, a C++23 compiler, SDL3, OpenGL, zlib and nlohmann-json. Dear ImGui
and stb are fetched by CMake at pinned commits.

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/citadel
```

On Arch: `pacman -S cmake ninja sdl3 zlib nlohmann-json`.

## Using it

citadel finds every game it knows in your Steam libraries and opens the one you used last.
The buttons beside its name switch between them. Each game has its own pack.

- **Choose a surface** on the left. The shelves (Posters, Signs, Billboards, Screens,
  Paintings, Menu backgrounds) are found by words in the game's material names. **Yours**
  lists what you have changed, and **All** lists everything.
- **Drop a picture** on the right, or click to choose one. It is cropped to fill the
  surface's frame. Drag it to move it, and scroll to zoom. Fill, Fit and Stretch, turning,
  and the sliders do the same with more control.
- **Edges**, for surfaces with see-through parts: **Clean** shows your whole picture and
  is the default. **Torn edges** keeps only the rips that reach in from the border.
  **Weathered** copies the original's whole see-through channel, grime and peeling
  included, which on most signs and posters is far more than the rips.
- **Where it appears** lists the maps that use the surface. **Play here** builds the pack
  and starts the game on that map.
- **Build pack** builds and installs the pack. **Play** builds it if anything changed,
  then starts the game. **Take pack out** removes it from the game.

Keys: Esc clears the search, then the selection. Q quits when you aren't typing.

## Where things are kept

- Your pack: `~/Documents/citadel/<game>/`. It holds `pack.json` and a `pictures/` folder
  with a copy of every picture you used, named by its content, so the pack is complete on
  its own and can be moved to another machine.
- Build files: `~/.cache/citadel/build/<game>/`. They can be deleted at any time.
- The installed pack:

  | Game | Pack |
  |---|---|
  | Half-Life 2 | `hl2/custom/citadel.vpk` |
  | Portal | `portal/custom/citadel.vpk` |
  | Portal 2 | `portal2_dlc3/pak01_dir.vpk` |
  | Quake III Arena | `baseq3/zz-citadel.pk3` |

## How the Half-Life 2 pack works

Half-Life 2, Episode One and Episode Two share one install, and each one's `gameinfo.txt`
mounts `hl2/custom/*` ahead of its own content. One pack there reaches all three.

For each surface, citadel finds the material's `$basetexture`, reads the game's texture,
and writes a new one at the same path. It goes in as sharp as your picture allows: the
game's size doubled as many times as the picture still has a pixel for every pixel, up to
4096 on a side. The studio shows the size under the previews.

Models, overlays and menu screens stretch a texture over the same place whatever its size.
Decals and map walls don't: the engine sizes them by their pixels. So every material that
draws a sharper texture is written into the pack too, with its `$decalscale` divided to
match, and a decal covers the same part of the wall as before. A texture that any map
paints on its walls keeps the game's size, because nothing in its material could shrink it
back. The texture's flags, such as clamping or having no mipmaps, are kept.

Several materials can draw one texture, such as two signs that share a sheet of metal. The
first surface in the pack keeps the game's texture path. A later surface on the same
texture gets a copy of its own under `materials/citadel/`, and its material is pointed
there, so each sign shows its own picture. It is written as VTF 7.2 in DXT1, or DXT5 when it has
see-through parts, with a full set of mipmaps unless the original had none.

Packs are made by Valve's own `vpk` tool, which ships with the game on Linux as
`bin/vpk_linux32`. That tool crashes when it exits, even after writing a good pack, so
citadel ignores its exit code and instead reads every file back out of the finished pack
and compares it with what it staged.

"Where it appears" reads each map's texture table and the decals and overlays its entities
place. Pictures painted onto models aren't found that way yet.

## How the Portal packs work

Portal is built like Half-Life 2: its `gameinfo.txt` mounts `portal/custom/*` first, and
its pack is made with the `vpk` tool that ships with it.

Portal 2 is different. Its `gameinfo.txt` lists no content: the engine mounts `update/`,
then each `portal2_dlcN` folder with the highest number first, then `portal2/`, each
one's `pak01_dir.vpk`. citadel's pack is `portal2_dlc3/pak01_dir.vpk`, which sits above
everything except `update/`, so a texture Valve patched in `update/` can't be replaced
this way. Portal 2 ships no `vpk` tool on Linux and reads version 1 packs, so citadel
writes that format itself, and reads the pack back to check it like any other.

## How the Quake III Arena pack works

Quake III keeps its art as TGA and JPG files inside `pk3` files, which are zip files, in
`baseq3/`. It loads the pk3s in order of their names, and when two hold the same path the
later one wins. citadel's pack is named `zz-citadel.pk3` so that it sorts after `pak0` to
`pak8`, and holds each new picture at the original's path, size and format.

Shader scripts in `scripts/*.shader` say which images each surface draws. "Where it
appears" reads them together with each map's shader list. The game is started through
Steam, which runs the Windows release under Proton.

## Checking the core

`citadel_check` runs everything except the window against the installed game: it mounts
the game, reads its art, writes a texture and reads it back, and builds a pack into a
folder you name. It never touches the game's folders.

```bash
./build/citadel_check ~/Pictures/some-photo.jpg /tmp/citadel-check
```

## License

GPL-3.0-only. See `LICENSE`. Dear ImGui (MIT) and stb (public domain or MIT) are fetched
at build time under their own licenses.
