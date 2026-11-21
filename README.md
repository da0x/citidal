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
| Portal, Portal 2 | next, on the same engine code |
| Quake III Arena | after those |

Each engine lives in its own folder under `source/engines/`, behind the interface in
`source/engine/game.hpp`. An engine knows how its games store art, where they are
installed, how a pack is built and installed, and how the game is started. The rest of
citadel (pictures, placement, packs, the studio) knows nothing about any one game.

## Platforms

Linux now. Windows is next: the platform code is in `source/platform/linux/`, and a
`source/platform/windows/` folder with the same file names will sit beside it.

## Building

Needs CMake 3.28 or later, a C++23 compiler, SDL3, OpenGL and nlohmann-json. Dear ImGui
and stb are fetched by CMake at pinned commits.

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/citadel
```

On Arch: `pacman -S cmake ninja sdl3 nlohmann-json`.

## Using it

citadel opens on the first game it finds in your Steam libraries.

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
- The installed pack, for Half-Life 2: `<game>/hl2/custom/citadel.vpk`.

## How the Half-Life 2 pack works

Half-Life 2, Episode One and Episode Two share one install, and each one's `gameinfo.txt`
mounts `hl2/custom/*` ahead of its own content. One pack there reaches all three.

For each surface, citadel finds the material's `$basetexture`, reads the game's texture,
and writes a new one at the same path and the same size, which matters because a decal's
size in the world comes from its texture's size. The texture's flags, such as clamping or
having no mipmaps, are kept. It is written as VTF 7.2 in DXT1, or DXT5 when it has
see-through parts, with a full set of mipmaps unless the original had none.

Packs are made by Valve's own `vpk` tool, which ships with the game on Linux as
`bin/vpk_linux32`. That tool crashes when it exits, even after writing a good pack, so
citadel ignores its exit code and instead reads every file back out of the finished pack
and compares it with what it staged.

"Where it appears" reads each map's texture table and the decals and overlays its entities
place. Pictures painted onto models aren't found that way yet.

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
