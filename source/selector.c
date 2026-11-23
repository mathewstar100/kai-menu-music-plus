// SPDX-License-Identifier: MIT
#include <switch.h>
#include <kaihen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "songs.h"
#include "control.h"

size_t __nx_heap_size=0x2000000;
#define MAX_SONGS 256
#define VISIBLE_SONGS 16
static char songs[MAX_SONGS][FS_MAX_PATH];
static size_t count,cursor;
static bool shuffle,pending_toggle;
static int screen_columns=80,screen_rows=45;
static char selected[FS_MAX_PATH],pending_name[FS_MAX_PATH];
static char message[128]="Choose a WAV, then press A.";

static int compare_names(const void *left,const void *right) { return strcmp(left,right); }
static Result refresh(FsFileSystem *sd) {
    song_read_shuffle(sd,&shuffle);
    count=0; FsDir dir;
    Result rc=fsFsOpenDirectory(sd,MUSIC_DIRECTORY,FsDirOpenMode_ReadFiles,&dir);
    if (R_FAILED(rc)) return rc;
    while (count<MAX_SONGS) {
        FsDirectoryEntry entry; s64 got=0;
        rc=fsDirRead(&dir,&got,1,&entry);
        if (R_FAILED(rc) || !got) break;
        if (got!=1 || !memchr(entry.name,0,sizeof entry.name) || !wave_filename(entry.name)) continue;
        if (strlen(MUSIC_DIRECTORY)+1+strlen(entry.name)>=FS_MAX_PATH) continue;
        strcpy(songs[count++],entry.name);
    }
    fsDirClose(&dir);
    qsort(songs,count,sizeof songs[0],compare_names);
    if (cursor>=count) cursor=count ? count-1 : 0;
    char path[FS_MAX_PATH];
    if (R_FAILED(song_resolve(sd,NULL,path,selected))) selected[0]=0;
    for (size_t i=0;i<count;i++) if (!strcmp(songs[i],selected)) { cursor=i; break; }
    return rc;
}
static Result open_plugin(Service *plugin) {
    Service ro={0},hen={0}; HenPluginName name={0};
    memcpy(name.name,"kai-menu-music",sizeof("kai-menu-music"));
    Result rc=kaihenGetRoSession(&ro);
    if (R_SUCCEEDED(rc)) rc=kaihenOpenHomebrewSession(&ro,&hen);
    if (R_SUCCEEDED(rc)) rc=henOpenPlugin(&hen,&name,plugin);
    if (serviceIsActive(&hen)) serviceClose(&hen);
    if (R_SUCCEEDED(rc)) {
        MusicState state={0}; rc=serviceDispatchOut(plugin,MusicCommand_State,state);
        if (R_SUCCEEDED(rc) && state.version!=1) rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);
        if (R_FAILED(rc)) serviceClose(plugin);
    }
    return rc;
}
static void move_to(int row,int column) { printf("\x1b[%d;%dH",row,column); }
static void centered_line(int row,int left,int width,const char *text) {
    int length=(int)strlen(text); if (length>width) length=width;
    move_to(row,left+(width-length)/2); printf("%.*s",length,text);
}
static void draw(bool waiting) {
    consoleClear();
    int width=screen_columns-4; if (width>68) width=68;
    int visible=(int)(count<VISIBLE_SONGS ? count : VISIBLE_SONGS); if (visible<2) visible=2;
    int height=visible+12;
    int left=(screen_columns-width)/2+1,top=(screen_rows-height-2)/2+1;
    for (int row=0;row<height;row++) {
        move_to(top+row,left); printf("\x1b[90m");
        if (row==0 || row==height-1) {
            putchar('+'); for (int i=0;i<width-2;i++) putchar('-'); putchar('+');
        } else { printf("|%*s|",width-2,""); }
        printf("\x1b[0m");
    }
    printf("\x1b[1;36m"); centered_line(top+1,left,width,"MENU MUSIC"); printf("\x1b[0m");
    move_to(top+3,left+2); printf("Shuffle: %s  [Y]",shuffle ? "ON" : "OFF");
    move_to(top+4,left+2);
    if (shuffle) printf("Playback: random song order");
    else printf("Selected: %.*s",width-14,selected[0] ? selected : "None");
    size_t first=(cursor/VISIBLE_SONGS)*VISIBLE_SONGS;
    for (int i=0;i<visible;i++) {
        size_t index=first+(size_t)i;
        if (index>=count) continue;
        move_to(top+6+i,left+2);
        if (index==cursor) printf("\x1b[44;97m");
        printf(" %c %-*.*s ",!shuffle && !strcmp(songs[index],selected) ? '*' : ' ',width-8,width-8,songs[index]);
        printf("\x1b[0m");
    }
    if (!count) {
        move_to(top+6,left+2); printf("No WAV files found.");
        move_to(top+7,left+2); printf("Add songs to " MUSIC_DIRECTORY "/");
    }
    if (count) {
        char position[32]; snprintf(position,sizeof position,"%u / %u",(unsigned)(cursor+1),(unsigned)count);
        centered_line(top+visible+7,left,width,position);
    }
    printf("\x1b[36m"); centered_line(top+visible+9,left+2,width-4,waiting ? "Applying..." : message); printf("\x1b[0m");
    centered_line(top+height+1,left,width,"Up/Down Browse  A Select  Y Shuffle  X Refresh  B/+ Exit");
}
int main(void) {
    PrintConsole *console=consoleInit(NULL);
    if (!console || !console->consoleInitialised) return 1;
    screen_columns=console->consoleWidth; screen_rows=console->consoleHeight;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    PadRepeater repeat; padRepeaterInitialize(&repeat,20,5);
    FsFileSystem sd;
    Result rc=fsOpenSdCardFileSystem(&sd);
    bool sd_open=R_SUCCEEDED(rc);
    if (sd_open) {
        // These are SD directories; all game/system content remains untouched.
        fsFsCreateDirectory(&sd,"/switch");
        fsFsCreateDirectory(&sd,MUSIC_ROOT);       // one level at a time: the parent must exist first
        fsFsCreateDirectory(&sd,MUSIC_DIRECTORY);
        rc=refresh(&sd);
    }
    if (R_FAILED(rc)) snprintf(message,sizeof message,"Could not read the music folder on SD.");
    Service plugin={0}; open_plugin(&plugin);
    bool dirty=true,waiting=false; u32 request=0; unsigned poll_ticks=0;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down=padGetButtonsDown(&pad);
        padRepeaterUpdate(&repeat,padGetButtons(&pad)&(HidNpadButton_AnyUp|HidNpadButton_AnyDown));
        u64 navigation=down|padRepeaterGetButtons(&repeat);
        if (down&(HidNpadButton_Plus|HidNpadButton_B)) break;
        if (count && (navigation&HidNpadButton_AnyUp)) { cursor=cursor ? cursor-1 : count-1; dirty=true; }
        if (count && (navigation&HidNpadButton_AnyDown)) { cursor=(cursor+1)%count; dirty=true; }
        if (!waiting && sd_open && (down&HidNpadButton_X)) {
            refresh(&sd); snprintf(message,sizeof message,"Song list refreshed."); dirty=true;
        }
        if (!waiting && sd_open && count && (down&HidNpadButton_Y)) {
            rc=song_save_shuffle(&sd,!shuffle);
            if (R_FAILED(rc)) snprintf(message,sizeof message,"Could not save shuffle mode to SD.");
            else {
                shuffle=!shuffle; pending_toggle=true; strcpy(pending_name,selected);
                if (!serviceIsActive(&plugin)) open_plugin(&plugin);
                if (serviceIsActive(&plugin)) rc=serviceDispatchOut(&plugin,MusicCommand_Reload,request);
                else rc=MAKERESULT(Module_Libnx,LibnxError_NotFound);
                if (R_SUCCEEDED(rc)) { waiting=true; poll_ticks=0; }
                else snprintf(message,sizeof message,"Mode saved. Restart the Switch to apply.");
            }
            dirty=true;
        }
        if (!waiting && sd_open && count && (down&HidNpadButton_A)) {
            Wave wave;
            rc=song_validate(&sd,songs[cursor],&wave);
            if (R_FAILED(rc)) {
                snprintf(message,sizeof message,"Use PCM16 stereo 48 kHz WAV, under 96 MiB.");
            } else {
                rc=song_save_selection(&sd,songs[cursor]);
                if (R_SUCCEEDED(rc)) rc=song_save_shuffle(&sd,false);
                if (R_FAILED(rc)) snprintf(message,sizeof message,"Could not save the selection to SD.");
                else {
                    shuffle=false; pending_toggle=false;
                    strcpy(pending_name,songs[cursor]);
                    if (!serviceIsActive(&plugin)) open_plugin(&plugin);
                    if (serviceIsActive(&plugin)) rc=serviceDispatchOut(&plugin,MusicCommand_Reload,request);
                    else rc=MAKERESULT(Module_Libnx,LibnxError_NotFound);
                    if (R_SUCCEEDED(rc)) { waiting=true; poll_ticks=0; }
                    else {
                        strcpy(selected,pending_name);
                        snprintf(message,sizeof message,"Saved. Restart the Switch to apply.");
                    }
                }
            }
            dirty=true;
        }
        if (waiting && !poll_ticks) {
            MusicState state={0}; rc=serviceDispatchOut(&plugin,MusicCommand_State,state);
            if (R_FAILED(rc)) {
                waiting=false; dirty=true;
                strcpy(selected,pending_name);
                snprintf(message,sizeof message,"Saved. Restart the Switch to apply.");
            } else if (state.completed==request) {
                waiting=false; dirty=true;
                if (R_SUCCEEDED(state.result)) {
                    strcpy(selected,pending_name);
                    snprintf(message,sizeof message,"%s",pending_toggle ? (shuffle ? "Shuffle on. Songs change automatically." : "Shuffle off. Selected song will repeat.") : "Selected. Return to HOME to listen.");
                } else snprintf(message,sizeof message,"Could not load this song. Choose another WAV.");
            }
            poll_ticks=6;
        }
        if (poll_ticks) --poll_ticks;
        if (dirty) { draw(waiting); dirty=false; }
        consoleUpdate(NULL);
        svcSleepThread(16666667);
    }
    if (serviceIsActive(&plugin)) serviceClose(&plugin);
    if (sd_open) fsFsClose(&sd);
    consoleExit(NULL); return 0;
}
