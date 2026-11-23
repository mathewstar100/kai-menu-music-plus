**kai-menu-music-plus** is a fork of [WeWaffles/kai-menu-music](https://github.com/WeWaffles/kai-menu-music). It adds separate music for the Settings, User, News, GameShare, Album and Controller pages, and keeps everything in a single `/switch/kai-menu-music/` folder. It replaces the original plugin; the two cannot be installed together.

You must restart and re-homebrew the console after putting the files on the sdcard for them to load.

Menu music for the Nintendo Switch 2. Drag a 48 kHz stereo PCM16 WAV to `/switch/kai-menu-music/home/yourfile.wav`

Command for converting your file `ffmpeg -i "input.mp3" -ar 48000 -ac 2 -c:a pcm_s16le "output.wav"` It must be under 96MB

Put the NRO in `/switch/kai-menu-music/`
Put the NSO in `/kaihen/plugins/`

## Folder layout

Everything lives in one folder:

```
/switch/kai-menu-music/
├── album/
├── controller/
├── gameshare/
├── news/
├── settings/
├── user/
├── home/
└── kai-menu-music-selector.nro
```

`home/` is the HOME menu's music. It can hold any number of WAVs, and the selector saves `selected.txt` and `shuffle.txt` there. The page folders (`album`, `controller`, `gameshare`, `news`, `settings`, `user`) are described below.

**Upgrading from the old layout:** move your HOME WAVs, `selected.txt` and `shuffle.txt` from `/switch/kai-menu-music/` into `/switch/kai-menu-music/home/`. If you had page folders or `applets.txt` in `/switch/kai-menu-music-selector/`, move them into `/switch/kai-menu-music/`, and put the NRO there too.

## Music for other HOME-menu pages

Each page can have its own song. Put one WAV (same format as above) in that page's folder under `/switch/kai-menu-music/`:

| Page | Folder |
| --- | --- |
| System Settings | `settings` |
| User page | `user` |
| News | `news` |
| GameShare | `gameshare` |
| Album | `album` |
| Controller settings | `controller` |

For example `/switch/kai-menu-music/album/yoursong.wav`. The song starts when the page opens, loops while it is open and stops when it closes. HOME then continues from where it left off. A page with no folder or no valid WAV stays silent. If a folder holds several WAVs, the first valid one in filename order is used. Pages only play when they were opened from HOME, so a controller prompt opened by a game stays silent.

All six pages are recognised out of the box. News and GameShare were matched to their applet ids by the order the pages were opened in, so if their songs come out swapped, swap them with `applets.txt` (see "Page music" in `BUILD.md`).

## Credits

- **[WeWaffles](https://github.com/WeWaffles)** created the original [kai-menu-music](https://github.com/WeWaffles/kai-menu-music): the HOME menu music plugin, the selector app and the focus detection that this fork is built on. This fork keeps the original MIT license (see `LICENSE`).
- **[ouncebrew](https://github.com/ouncebrew)** makes [kaiHEN](https://github.com/ouncebrew/kaihen) (libkaihen), [libounce](https://github.com/ouncebrew/libounce) and [ounce-devkit](https://github.com/ouncebrew/ounce-devkit), which the plugin and selector are built with.
- **[libnx](https://github.com/switchbrew/libnx)** and its authors, whose license is included with this project.

The licenses of these libraries are in the `licenses/` folder.
