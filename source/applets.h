// SPDX-License-Identifier: MIT
#pragma once
#include <switch.h>
#include "core.h"
#include "songs.h"

// Extra music for HOME-menu pages. Each page owns a folder under APPLET_DIRECTORY that holds one WAV.
#define APPLET_DIRECTORY MUSIC_ROOT
#define APPLET_CONFIG APPLET_DIRECTORY "/applets.txt"  // optional: how each page is recognised
#define APPLET_LOG APPLET_DIRECTORY "/applet-log.txt"  // optional: create it to record focus events
#define APPLET_LOG_LIMIT (256u*1024u)
#define APPLET_RULES 6

// What the plugin is playing for: HOME, or page n (rules[n]) as CONTEXT_APPLET+n.
enum { CONTEXT_NONE=-1, CONTEXT_HOME=0, CONTEXT_APPLET=1 };

// A page is recognised by the PDM applet id and, when program is not 0, the program id too.
// Built-in ids can be replaced in APPLET_CONFIG. A rule that is not enabled never matches.
typedef struct { const char *folder; bool enabled; unsigned applet; uint64_t program; } AppletRule;

// What the PDM event history says is in front. `chain` is true while the foreground was reached
// from HOME, so a controller prompt opened by a game never gets menu music.
typedef struct { bool home, focused, chain; unsigned applet; uint64_t program; } Focus;

void applet_rules_default(AppletRule rules[APPLET_RULES]);
// Lines of `folder = applet [program]` in hex; '#' starts a comment. Bad lines and unknown folders are ignored.
void applet_rules_parse(AppletRule rules[APPLET_RULES],const char *text,size_t length);
Result applet_rules_read(FsFileSystem *sd,AppletRule rules[APPLET_RULES]);

// Picks the first valid WAV (filename order) in APPLET_DIRECTORY/<folder>. Read-only.
Result applet_find_track(FsFileSystem *sd,const char *folder,char path[FS_MAX_PATH]);

// PDM stores the two halves of a program id swapped: word 0 is the high half.
uint64_t program_id_make(uint32_t first,uint32_t second);
// Feed one PDM play event, oldest first, starting from a zeroed Focus. HOME handling is core's focus_step.
void focus_update(Focus *focus,unsigned kind,unsigned applet,uint64_t program,unsigned event);
int focus_context(const Focus *focus,const AppletRule rules[APPLET_RULES]);

// Discovery log. Only written when the user has created APPLET_LOG, and capped at APPLET_LOG_LIMIT.
size_t applet_log_format(char *out,size_t capacity,int index,unsigned applet,uint64_t program,unsigned event);
Result applet_log_append(FsFileSystem *sd,const char *text,size_t length);
