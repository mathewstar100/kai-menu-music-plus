// SPDX-License-Identifier: MIT
#include <switch.h>
#include <kaihen/plugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "core.h"
#include "songs.h"
#include "applets.h"
#include "control.h"

u32 __nx_applet_type = AppletType_None;
size_t __nx_heap_size = 0x6800000; // bounded track + runtime + audio buffers
void __appInit(void) {}
void __appExit(void) {}
#define BLOCK 4096u // 1024 stereo frames, about 21 ms
static unsigned char pcm[2][BLOCK] __attribute__((aligned(4096)));
static AudioOutBuffer buffers[2];

static Result initialize_version(void) {
    Result rc=setsysInitialize();
    if (R_FAILED(rc)) return rc;
    SetSysFirmwareVersion fw;
    rc=setsysGetFirmwareVersion(&fw);
    if (R_SUCCEEDED(rc)) hosversionSet(MAKEHOSVERSION(fw.major,fw.minor,fw.micro));
    setsysExit();
    return rc;
}
static Result bad_input(void) { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
typedef struct { Mutex lock; MusicState state; } MusicControl;
static MusicControl control={.state={.version=1}};

static Result control_command(u32 command,const void *input,size_t input_size,void *output,size_t *output_size,void *context) {
    (void)input; (void)input_size; (void)context;
    mutexLock(&control.lock);
    if (command==MusicCommand_State) {
        memcpy(output,&control.state,sizeof control.state); *output_size=sizeof control.state;
    } else if (command==MusicCommand_Reload) {
        if (++control.state.requested==0) ++control.state.requested;
        memcpy(output,&control.state.requested,sizeof control.state.requested); *output_size=sizeof control.state.requested;
    } else { mutexUnlock(&control.lock); return bad_input(); }
    mutexUnlock(&control.lock); return 0;
}
static void completed(u32 generation,Result result) {
    mutexLock(&control.lock); control.state.completed=generation; control.state.result=result; mutexUnlock(&control.lock);
}
static u32 requested(void) {
    mutexLock(&control.lock); u32 result=control.state.requested; mutexUnlock(&control.lock); return result;
}
// The plugin opens only read handles; the selector app owns the SD setting.
// The one exception is the optional discovery log, which exists only if the user created it.
static Result read_track(FsFileSystem *sd,const char *path,unsigned char **bytes,Wave *wave) {
    *bytes=NULL; *wave=(Wave){0};
    FsFile file;
    Result rc=fsFsOpenFile(sd,path,FsOpenMode_Read,&file);
    if (R_SUCCEEDED(rc)) {
        s64 size=0;
        rc=fsFileGetSize(&file,&size);
        if (R_SUCCEEDED(rc) && (size<12 || size>MAX_TRACK_BYTES)) rc=bad_input();
        if (R_SUCCEEDED(rc)) {
            *bytes=malloc((size_t)size);
            if (!*bytes) rc=MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);
        }
        if (R_SUCCEEDED(rc)) {
            u64 got=0; rc=fsFileRead(&file,0,*bytes,(u64)size,FsReadOption_None,&got);
            if (R_SUCCEEDED(rc) && (got!=(u64)size || !wave_parse(*bytes,(size_t)size,wave))) rc=bad_input();
        }
        fsFileClose(&file);
    }
    if (R_FAILED(rc)) { free(*bytes); *bytes=NULL; *wave=(Wave){0}; }
    return rc;
}
static Result load_track(const char *requested_name,unsigned char **bytes,Wave *wave,char active_name[FS_MAX_PATH]) {
    *bytes=NULL; *wave=(Wave){0};
    FsFileSystem sd;
    Result rc=fsOpenSdCardFileSystem(&sd);
    if (R_FAILED(rc)) return rc;
    char path[FS_MAX_PATH];
    rc=song_resolve(&sd,requested_name,path,active_name);
    if (R_SUCCEEDED(rc)) rc=read_track(&sd,path,bytes,wave);
    fsFsClose(&sd);
    return rc;
}
// Stops audio and frees the resident track. Only one track fits in the heap at a time.
static void release_track(unsigned char **track,bool *audio,bool *started) {
    if (*audio) { if (*started) audoutStopAudioOut(); audoutExit(); }
    *audio=*started=false; free(*track); *track=NULL;
}
static uint32_t shuffle_random(void *context) { (void)context; return (uint32_t)randomGet64(); }
static Result choose_playback(bool *shuffle,const char *previous,char name[FS_MAX_PATH]) {
    FsFileSystem sd; Result rc=fsOpenSdCardFileSystem(&sd);
    if (R_FAILED(rc)) return rc;
    song_read_shuffle(&sd,shuffle);
    char path[FS_MAX_PATH]; Wave wave;
    if (*shuffle) rc=song_choose_shuffle(&sd,previous,shuffle_random,NULL,name);
    else rc=song_resolve(&sd,NULL,path,name);
    if (R_SUCCEEDED(rc)) rc=song_validate(&sd,name,&wave);
    fsFsClose(&sd); return rc;
}
static Result audio_initialize(void) {
    Result rc=audoutInitialize();
    if (R_SUCCEEDED(rc) && (audoutGetSampleRate()!=48000 || audoutGetChannelCount()!=2 || audoutGetPcmFormat()!=PcmFormat_Int16)) {
        audoutExit(); rc=bad_input();
    }
    return rc;
}
// Optional discovery log: appends each new applet event so the ids of HOME-menu pages can be found.
static void log_events(const PdmPlayEvent *events,s32 start,s32 count,s32 *logged) {
    static char text[8192]; size_t length=0;
    for (s32 i=0;i<count;i++) {
        const PdmPlayEvent *e=&events[i];
        if (start+i<=*logged || e->play_event_type!=0) continue;
        const u32 *id=e->event_data.applet.program_id;
        length+=applet_log_format(text+length,sizeof text-length,(int)(start+i),e->event_data.applet.applet_id,
            program_id_make(id[0],id[1]),e->event_data.applet.event_type);
    }
    *logged=start+count-1;
    if (!length || R_FAILED(fsInitialize())) return;
    FsFileSystem sd;
    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&sd))) { applet_log_append(&sd,text,length); fsFsClose(&sd); }
    fsExit();
}
// Read the recent raw event window and work out which HOME-menu page is in front: HOME, one of
// the applet rules, or neither. Any error or unrecognized state is silent.
// No writes or notifications are sent to PDM.
static int poll_context(const AppletRule rules[APPLET_RULES],s32 *logged) {
    s32 total=0, first=0, last=0, count=0;
    if (R_FAILED(pdmqryGetAvailablePlayEventRange(&total,&first,&last)) || total<=0) return CONTEXT_NONE;
    s32 start=last>63 ? last-63 : 0;
    if (start<first) start=first;
    PdmPlayEvent events[64];
    if (R_FAILED(pdmqryQueryPlayEvent(start,events,64,&count)) || count<=0 || count>64) return CONTEXT_NONE;
    Focus focus={0};
    for (s32 i=0;i<count;i++) {
        PdmPlayEvent *e=&events[i];
        const u32 *id=e->event_data.applet.program_id;
        focus_update(&focus,e->play_event_type,e->event_data.applet.applet_id,program_id_make(id[0],id[1]),
            e->event_data.applet.event_type);
    }
    if (logged) log_events(events,start,count,logged);
    return focus_context(&focus,rules);
}
// Page rules and the log switch live on the SD card; both are optional.
static void read_applet_settings(AppletRule rules[APPLET_RULES],bool *log_on) {
    FsFileSystem sd; if (R_FAILED(fsOpenSdCardFileSystem(&sd))) return;
    applet_rules_read(&sd,rules);
    FsDirEntryType type;
    *log_on=R_SUCCEEDED(fsFsGetEntryType(&sd,APPLET_LOG,&type)) && type==FsDirEntryType_File;
    if (*log_on) applet_log_append(&sd,"--- plugin started ---\n",strlen("--- plugin started ---\n"));
    fsFsClose(&sd);
}
static size_t fill(unsigned i,const unsigned char *data,size_t size,size_t *cursor,bool loop) {
    size_t done=wave_copy_block(pcm[i],BLOCK,data,size,cursor,loop);
    for (size_t p=0;p<done;p+=2) {
        int16_t sample;
        memcpy(&sample,pcm[i]+p,2); sample/=2; memcpy(pcm[i]+p,&sample,2);
    }
    armDCacheFlush(pcm[i],BLOCK); return done;
}
int main(void) {
    Handle ready=INVALID_HANDLE,session=INVALID_HANDLE;
    PluginService service={0};
    Result rc=pluginRuntimeInitializeWithService(&ready,&session);
    if (R_FAILED(rc)) return rc;
    rc=pluginServiceStart(&service,session,control_command,NULL);
    if (R_SUCCEEDED(rc)) rc=svcSignalEvent(ready);
    if (R_SUCCEEDED(rc)) rc=initialize_version();
    bool pdm=false,audio=false,started=false,shuffle=false,advance=false;
    if (R_SUCCEEDED(rc)) { rc=pdmqryInitialize(); pdm=R_SUCCEEDED(rc); }
    unsigned char *track=NULL; Wave wave={0}; char active_name[FS_MAX_PATH]={0}; // active_name is always HOME's song
    AppletRule rules[APPLET_RULES]; applet_rules_default(rules);
    bool log_on=false;
    if (R_SUCCEEDED(rc)) {
        Result load=fsInitialize();
        if (R_SUCCEEDED(load)) {
            read_applet_settings(rules,&log_on);
            char initial[FS_MAX_PATH]; load=choose_playback(&shuffle,NULL,initial);
            if (R_SUCCEEDED(load)) load=load_track(initial,&track,&wave,active_name);
            fsExit();
        }
        if (R_SUCCEEDED(load)) { load=audio_initialize(); audio=R_SUCCEEDED(load); }
        completed(0,load); // Missing music stays idle; the selector can load it later.
    }
    size_t cursor=0,home_cursor=0; bool queued[2]={false,false};
    unsigned focus_ticks=0; u32 generation=0; s32 logged=-1;
    // context: where the user is now. loaded: whose song is in memory. HOME's place is parked in
    // home_cursor while a page's song is loaded in its stead.
    int context=CONTEXT_NONE,previous_context=CONTEXT_NONE,loaded=track ? CONTEXT_HOME : CONTEXT_NONE;
    for (unsigned i=0;i<2;i++) buffers[i]=(AudioOutBuffer){.buffer=pcm[i],.buffer_size=BLOCK,.data_size=BLOCK};
    while (R_SUCCEEDED(rc)) {
        u32 next=requested();
        if (next!=generation || advance) {
            bool user_request=next!=generation;
            Result load=fsInitialize();
            if (R_SUCCEEDED(load)) {
                bool next_shuffle=shuffle; char chosen[FS_MAX_PATH];
                load=choose_playback(&next_shuffle,user_request ? NULL : active_name,chosen);
                if (R_SUCCEEDED(load)) {
                    char previous[FS_MAX_PATH]; strcpy(previous,active_name);
                    release_track(&track,&audio,&started);
                    load=load_track(chosen,&track,&wave,active_name);
                    if (R_FAILED(load) && previous[0]) load_track(previous,&track,&wave,active_name);
                    if (track) {
                        Result ar=audio_initialize(); audio=R_SUCCEEDED(ar);
                        if (R_SUCCEEDED(load)) load=ar;
                    }
                    if (R_SUCCEEDED(load)) shuffle=next_shuffle;
                    cursor=home_cursor=0; queued[0]=queued[1]=false; focus_ticks=0;
                    loaded=track ? CONTEXT_HOME : CONTEXT_NONE;
                }
                fsExit();
            }
            if (user_request) { generation=next; completed(generation,load); }
            else if (R_FAILED(load)) { shuffle=false; cursor=0; }
            advance=false;
        }
        if (!focus_ticks) { context=poll_context(rules,log_on ? &logged : NULL); focus_ticks=10; }
        --focus_ticks;
        if (context!=previous_context) {
            previous_context=context;
            if (context>=CONTEXT_APPLET && context==loaded) cursor=0; // a reopened page starts its song over
            else if (context!=CONTEXT_NONE && context!=loaded && R_SUCCEEDED(fsInitialize())) {
                // Only one track fits in memory, so swap tracks and remember HOME's place.
                if (context==CONTEXT_HOME) {
                    char wanted[FS_MAX_PATH]; strcpy(wanted,active_name);
                    release_track(&track,&audio,&started); loaded=CONTEXT_NONE;
                    queued[0]=queued[1]=false; cursor=home_cursor;
                    Result load=wanted[0] ? load_track(wanted,&track,&wave,active_name) : bad_input();
                    if (R_FAILED(load)) { // HOME's song went missing: choose again
                        char chosen[FS_MAX_PATH]; bool next_shuffle=shuffle;
                        load=choose_playback(&next_shuffle,NULL,chosen);
                        if (R_SUCCEEDED(load)) load=load_track(chosen,&track,&wave,active_name);
                        if (R_SUCCEEDED(load)) shuffle=next_shuffle;
                        cursor=0;
                    }
                    if (R_SUCCEEDED(load)) {
                        if (cursor>wave.size || cursor%4) cursor=0;
                        Result ar=audio_initialize(); audio=R_SUCCEEDED(ar);
                        loaded=CONTEXT_HOME;
                    }
                } else {
                    FsFileSystem sd; char path[FS_MAX_PATH];
                    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&sd))) {
                        // Look first, so a page without music leaves HOME's song in memory.
                        if (R_SUCCEEDED(applet_find_track(&sd,rules[context-CONTEXT_APPLET].folder,path))) {
                            if (loaded==CONTEXT_HOME) home_cursor=cursor;
                            release_track(&track,&audio,&started); loaded=CONTEXT_NONE;
                            queued[0]=queued[1]=false;
                            if (R_SUCCEEDED(read_track(&sd,path,&track,&wave))) {
                                Result ar=audio_initialize(); audio=R_SUCCEEDED(ar);
                                loaded=context; cursor=0;
                            }
                        }
                        fsFsClose(&sd);
                    }
                }
                fsExit();
            }
            focus_ticks=0;
        }
        // Play only while the page in front owns the song in memory; anything else is silent.
        if (!track || !audio || context==CONTEXT_NONE || context!=loaded) {
            if (started) { rc=audoutStopAudioOut(); started=false; }
            focus_ticks=0; svcSleepThread(50000000); continue;
        }
        AudioOutBuffer *released=NULL; u32 count=0;
        do {
            rc=audoutGetReleasedAudioOutBuffer(&released,&count);
            if (R_FAILED(rc) || !count) break;
            if (released==&buffers[0]) queued[0]=false;
            else if (released==&buffers[1]) queued[1]=false;
            else { rc=bad_input(); break; }
        } while (count);
        if (R_FAILED(rc)) break;
        bool advances=shuffle && loaded==CONTEXT_HOME; // shuffle moves HOME along; page songs loop
        if (advances && cursor>=wave.size && !queued[0] && !queued[1]) { advance=true; continue; }
        for (unsigned i=0;i<2 && R_SUCCEEDED(rc);i++) {
            if (queued[i]) continue;
            size_t length=fill(i,track+wave.offset,wave.size,&cursor,!advances);
            if (!length) continue;
            buffers[i].data_size=length;
            rc=audoutAppendAudioOutBuffer(&buffers[i]);
            if (R_SUCCEEDED(rc)) queued[i]=true;
        }
        if (R_SUCCEEDED(rc) && !started) { rc=audoutStartAudioOut(); started=R_SUCCEEDED(rc); }
        svcSleepThread(5000000);
    }
    if (audio) { if (started) audoutStopAudioOut(); audoutExit(); }
    if (pdm) pdmqryExit();
    if (service.started) pluginServiceStop(&service);
    free(track); pluginRuntimeExit(); return rc;
}
