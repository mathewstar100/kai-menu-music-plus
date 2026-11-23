// SPDX-License-Identifier: MIT
#include "applets.h"
#include "songs.h"
#include <stdio.h>
#include <string.h>

static Result bad_input(void) { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }

void applet_rules_default(AppletRule rules[APPLET_RULES]) {
    // Ids seen in an applet-log.txt from a Switch 2. Every page reports HOME's own program id
    // (0400000000001000), so only the applet id tells them apart. Album, controller and user are
    // libounce's AppletId names. Settings is libounce's "set" applet. News and GameShare were
    // matched by the order the pages were opened in, so they could be the other way around:
    // swap them in APPLET_CONFIG if so.
    rules[0]=(AppletRule){"settings",true,0x16,0};    // AppletId_LibraryAppletSet
    rules[1]=(AppletRule){"user",true,0x1A,0};        // AppletId_LibraryAppletMyPage
    rules[2]=(AppletRule){"news",true,0x23,0};
    rules[3]=(AppletRule){"gameshare",true,0x24,0};
    rules[4]=(AppletRule){"album",true,0x15,0};       // AppletId_LibraryAppletPhotoViewer
    rules[5]=(AppletRule){"controller",true,0x0C,0};  // AppletId_LibraryAppletController
}

static bool blank(char c) { return c==' ' || c=='\t' || c=='\r'; }
static int hex_digit(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
// Hex number with an optional 0x prefix and at most 16 digits.
static bool hex_number(const char **text,uint64_t *value) {
    const char *p=*text; uint64_t v=0; unsigned digits=0; int d;
    if (p[0]=='0' && (p[1]=='x' || p[1]=='X') && hex_digit(p[2])>=0) p+=2;
    while ((d=hex_digit(*p))>=0) {
        if (digits==16) return false;
        v=v<<4|(uint64_t)d; ++digits; ++p;
    }
    if (!digits) return false;
    *value=v; *text=p; return true;
}
static bool same_name(const char *a,const char *b) {
    for (;*a && *b;++a,++b) {
        char x=*a,y=*b;
        if (x>='A' && x<='Z') x=(char)(x+('a'-'A'));
        if (y>='A' && y<='Z') y=(char)(y+('a'-'A'));
        if (x!=y) return false;
    }
    return *a==*b;
}
static void parse_line(AppletRule rules[APPLET_RULES],char *line) {
    char *name=line; while (blank(*name)) ++name;
    char *end=name; while (*end && !blank(*end) && *end!='=') ++end;
    char *p=end; while (blank(*p)) ++p;
    if (end==name || *p!='=') return;
    ++p; *end=0;
    const char *q=p; uint64_t applet=0,program=0;
    while (blank(*q)) ++q;
    if (!hex_number(&q,&applet) || applet>0xFF) return;
    while (blank(*q)) ++q;
    if (*q) {
        if (!hex_number(&q,&program)) return;
        while (blank(*q)) ++q;
        if (*q) return;
    }
    for (unsigned i=0;i<APPLET_RULES;i++) {
        if (!same_name(rules[i].folder,name)) continue;
        rules[i].enabled=true; rules[i].applet=(unsigned)applet; rules[i].program=program;
    }
}
void applet_rules_parse(AppletRule rules[APPLET_RULES],const char *text,size_t length) {
    if (!rules || !text) return;
    for (size_t pos=0;pos<length;++pos) {
        char line[96]; size_t n=0; bool comment=false,overflow=false;
        for (;pos<length && text[pos]!='\n';++pos) {
            if (text[pos]=='#') comment=true;
            if (comment) continue;
            if (n+1<sizeof line) line[n++]=text[pos]; else overflow=true;
        }
        line[n]=0;
        if (!overflow && strlen(line)==n) parse_line(rules,line);
    }
}
Result applet_rules_read(FsFileSystem *sd,AppletRule rules[APPLET_RULES]) {
    FsFile file; Result rc=fsFsOpenFile(sd,APPLET_CONFIG,FsOpenMode_Read,&file);
    if (R_FAILED(rc)) return rc;
    char text[2048]; s64 size=0; u64 got=0;
    rc=fsFileGetSize(&file,&size);
    if (R_SUCCEEDED(rc) && (size<1 || size>(s64)sizeof text)) rc=bad_input();
    if (R_SUCCEEDED(rc)) rc=fsFileRead(&file,0,text,(u64)size,FsReadOption_None,&got);
    if (R_SUCCEEDED(rc) && got!=(u64)size) rc=bad_input();
    if (R_SUCCEEDED(rc)) applet_rules_parse(rules,text,(size_t)size);
    fsFileClose(&file); return rc;
}

static bool file_read(void *context,size_t offset,void *output,size_t length) {
    u64 got=0;
    return R_SUCCEEDED(fsFileRead(context,(s64)offset,output,length,FsReadOption_None,&got)) && got==length;
}
static bool valid_track(FsFileSystem *sd,const char *path) {
    FsFile file; if (R_FAILED(fsFsOpenFile(sd,path,FsOpenMode_Read,&file))) return false;
    s64 size=0; Wave wave;
    bool ok=R_SUCCEEDED(fsFileGetSize(&file,&size)) && size>=12 && size<=MAX_TRACK_BYTES &&
        wave_probe(file_read,&file,(size_t)size,&wave);
    fsFileClose(&file); return ok;
}
Result applet_find_track(FsFileSystem *sd,const char *folder,char path[FS_MAX_PATH]) {
    char directory[FS_MAX_PATH],best[FS_MAX_PATH]; best[0]=0;
    int length=snprintf(directory,sizeof directory,"%s/%s",APPLET_DIRECTORY,folder);
    if (length<0 || (size_t)length>=sizeof directory) return bad_input();
    FsDir dir; Result rc=fsFsOpenDirectory(sd,directory,FsDirOpenMode_ReadFiles,&dir);
    if (R_FAILED(rc)) return rc;
    for (;;) {
        FsDirectoryEntry entry; s64 count=0; char candidate[FS_MAX_PATH];
        rc=fsDirRead(&dir,&count,1,&entry);
        if (R_FAILED(rc) || !count) break;
        if (count!=1 || !memchr(entry.name,0,sizeof entry.name)) { rc=bad_input(); break; }
        if (!wave_filename(entry.name) || (best[0] && strcmp(entry.name,best)>=0)) continue;
        int n=snprintf(candidate,sizeof candidate,"%s/%s",directory,entry.name);
        if (n<0 || (size_t)n>=sizeof candidate || !valid_track(sd,candidate)) continue;
        strcpy(best,entry.name);
    }
    fsDirClose(&dir);
    if (R_FAILED(rc)) return rc;
    if (!best[0]) return MAKERESULT(Module_Libnx,LibnxError_NotFound);
    int n=snprintf(path,FS_MAX_PATH,"%s/%s",directory,best);
    return n<0 || (size_t)n>=FS_MAX_PATH ? bad_input() : 0;
}

uint64_t program_id_make(uint32_t first,uint32_t second) { return (uint64_t)first<<32 | second; }
static bool same_applet(const Focus *f,unsigned applet,uint64_t program) {
    return f->applet==applet && (applet==3 || f->program==program); // HOME is just applet 3
}
void focus_update(Focus *f,unsigned kind,unsigned applet,uint64_t program,unsigned event) {
    f->home=focus_step(f->home,kind,applet,event); // HOME keeps exactly its old rules
    if (kind==2 || kind==4) { *f=(Focus){0}; return; } // power or log start: forget everything
    if (kind!=0) return;
    if (event==2) {
        f->focused=true; f->applet=applet; f->program=program;
        if (applet==3) f->chain=true;        // HOME: whatever opens next came from the menu
        else if (applet==1) f->chain=false;  // a game: applets it opens are the game's own
    } else if (f->focused && same_applet(f,applet,program) && (event==1 || (event>=3 && event<=6))) {
        f->focused=false;
    }
}
int focus_context(const Focus *f,const AppletRule rules[APPLET_RULES]) {
    if (f->home) return CONTEXT_HOME;
    if (!f->focused || !f->chain) return CONTEXT_NONE;
    for (int i=0;i<APPLET_RULES;i++) {
        const AppletRule *r=&rules[i];
        if (r->enabled && r->applet==f->applet && (!r->program || r->program==f->program)) return CONTEXT_APPLET+i;
    }
    return CONTEXT_NONE;
}

static const char *event_name(unsigned event) {
    switch (event) {
    case 0: return "launch";
    case 1: case 5: case 6: return "exit";
    case 2: return "in_focus";
    case 3: case 4: return "out_of_focus";
    default: return "unknown";
    }
}
size_t applet_log_format(char *out,size_t capacity,int index,unsigned applet,uint64_t program,unsigned event) {
    int n=snprintf(out,capacity,"#%d applet=0x%02X program=%016llX event=%u %s\n",
        index,applet,(unsigned long long)program,event,event_name(event));
    return n>0 && (size_t)n<capacity ? (size_t)n : 0;
}
Result applet_log_append(FsFileSystem *sd,const char *text,size_t length) {
    FsFile file; Result rc=fsFsOpenFile(sd,APPLET_LOG,FsOpenMode_Write,&file); // fails unless the user made the file
    if (R_FAILED(rc)) return rc;
    s64 size=0; rc=fsFileGetSize(&file,&size);
    if (R_SUCCEEDED(rc) && size+(s64)length<=(s64)APPLET_LOG_LIMIT) {
        rc=fsFileSetSize(&file,size+(s64)length);
        if (R_SUCCEEDED(rc)) rc=fsFileWrite(&file,size,text,(u64)length,FsWriteOption_Flush);
    }
    fsFileClose(&file); return rc;
}
