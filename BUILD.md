# Build and install

Build with [ounce-devkit](https://github.com/ouncebrew/ounce-devkit), libounce and libkaihen `20261117.1-1`:

```sh
make
```

Install:

| File | SD path |
| --- | --- |
| `kai-menu-music.nso` | `/kaihen/plugins/kai-menu-music.nso` |
| `kai-menu-music-selector.nro` | `/switch/kai-menu-music/kai-menu-music-selector.nro` |

Replace the existing plugin, restart kaiHEN, and open **Menu Music** from the homebrew menu.

| Control | Action |
| --- | --- |
| Up/Down | Browse |
| A | Select a song and turn shuffle off |
| Y | Toggle shuffle |
| X | Refresh |
| B / + | Exit |

Return to HOME to listen. Selection and shuffle settings are saved on SD in `/switch/kai-menu-music/home/selected.txt` and `/switch/kai-menu-music/home/shuffle.txt`. Shuffle skips unsupported files and avoids immediate repeats when another valid song exists.

Put HOME's WAV files in `/switch/kai-menu-music/home/`. Format: RIFF/WAVE, PCM signed 16-bit, stereo, 48000 Hz, up to 96 MiB.

```sh
ffmpeg -i your-song.mp3 -ar 48000 -ac 2 -c:a pcm_s16le yourfile.wav
```

The plugin loads one track into RAM and plays on HOME. The selector can request a live reload. Without a saved choice, the first visible WAV in filename order is used. Volume is fixed at half amplitude. No NAND writes, network access, or overlay. The only SD write is the optional discovery log below, and only if you create it.

## Page music

Besides HOME, the plugin can play one song for each of six pages: `settings`, `user`, `news`, `gameshare`, `album` and `controller`. Each page reads the first valid WAV (filename order) in `/switch/kai-menu-music/<folder>/`. The folder is looked at each time the page opens, so changing the file needs no restart. The selector app does not manage these songs.

- The song starts at the beginning when the page opens, loops, and stops when the page closes or loses focus.
- Only one track fits in memory, so opening a page with a song reloads from SD (a short delay) and returning to HOME reloads HOME's song and resumes its position. A page without a song leaves HOME's song in memory.
- A page only plays when the foreground was reached from HOME. A page opened by a game (for example its controller prompt) stays silent.
- HOME behaviour, shuffle and the selector are unchanged. Page folders are separate from `home/`, so shuffle never picks page songs. Only `home/` is meant to hold several WAVs.

### Applet ids

The plugin recognises a page by the applet id in the PDM play events. On a Switch 2 every page reports HOME's own program id (`0400000000001000`), so only the applet id tells them apart. Built in:

| Folder | Applet id | Source |
| --- | --- | --- |
| `settings` | 16 | libounce's `set` applet, seen in the log |
| `user` | 1A | libounce's `MyPage`, seen in the log |
| `news` | 23 | seen in the log, matched by the order pages were opened |
| `gameshare` | 24 | seen in the log, matched by the order pages were opened |
| `album` | 15 | libounce's `PhotoViewer`, seen in the log |
| `controller` | 0C | libounce's `Controller`, seen in the log |

The log cannot say which of 23 and 24 is News and which is GameShare. If those two songs are swapped, put this in `applets.txt`:

```
news = 24
gameshare = 23
```

To set or override any id, create `/switch/kai-menu-music/applets.txt`. Values are hex, `#` starts a comment, and the program id is optional (only needed if two pages ever share an applet id):

```
# folder = applet_id [program_id]
settings = 16
news = 20 0100000000001000
```

The file is read at startup, so restart kaiHEN after editing it. If a song plays on a page it does not belong to, two pages share an applet id. Use the log below to check.

### Finding ids

1. Create an empty file `/switch/kai-menu-music/applet-log.txt` and restart kaiHEN.
2. From HOME, open each page once (Settings, News, GameShare, Album, ...), then return to HOME.
3. Read the file. Each line is one applet event: `#index applet=0x.. program=................ event=N name`. HOME shows `applet=0x03 program=0400000000001000`, which confirms the log is working. The lines between HOME going `out_of_focus` and coming back `in_focus` belong to the page you opened.
4. Put that page's `applet` (and `program`, if needed) in `applets.txt`. A page needs an `in_focus` line to be recognised. If one only shows `launch`/`exit`, the plugin cannot see it yet.
5. Delete the log when done. It stops growing at 256 KiB.

## Tests

Host tests:

```sh
clang -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined source/core.c tests/test_core.c -o /tmp/menu-core-test
/tmp/menu-core-test
clang -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/mock source/core.c source/songs.c tests/test_songs.c -o /tmp/menu-songs-test
/tmp/menu-songs-test
clang -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/mock source/core.c source/applets.c tests/test_applets.c -o /tmp/menu-applets-test
/tmp/menu-applets-test
```

Source is MIT licensed. Dependency notices are in `licenses/`. Music and console assets are not included.
