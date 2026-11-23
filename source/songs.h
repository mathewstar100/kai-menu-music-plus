#pragma once
#include <switch.h>
#include "core.h"
// Everything lives under MUSIC_ROOT: one folder per HOME-menu page, the selector NRO, and "home".
#define MUSIC_ROOT "/switch/kai-menu-music"
// HOME's songs (any number of WAVs) and its two setting files.
#define MUSIC_DIRECTORY MUSIC_ROOT "/home"
#define MUSIC_SELECTION MUSIC_DIRECTORY "/selected.txt"
#define MUSIC_SHUFFLE MUSIC_DIRECTORY "/shuffle.txt"
#define MAX_TRACK_BYTES (96u*1024u*1024u)
Result song_read_selection(FsFileSystem *sd,char name[FS_MAX_PATH]);
Result song_save_selection(FsFileSystem *sd,const char *name);
Result song_resolve(FsFileSystem *sd,const char *explicit_name,char path[FS_MAX_PATH],char name[FS_MAX_PATH]);
Result song_validate(FsFileSystem *sd,const char *name,Wave *wave);
Result song_read_shuffle(FsFileSystem *sd,bool *enabled);
Result song_save_shuffle(FsFileSystem *sd,bool enabled);
typedef uint32_t (*SongRandom)(void *context);
Result song_choose_shuffle(FsFileSystem *sd,const char *previous,SongRandom random,void *context,char name[FS_MAX_PATH]);
