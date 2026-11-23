// SPDX-License-Identifier: MIT
#include "../source/applets.h"
#include "../source/songs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ---- mock SD card -------------------------------------------------------------------------------
static const unsigned char wav[]={
 'R','I','F','F',40,0,0,0,'W','A','V','E',
 'f','m','t',' ',16,0,0,0,1,0,2,0,0x80,0xbb,0,0,0,0xee,2,0,4,0,16,0,
 'd','a','t','a',4,0,0,0,0,0,0,0};
static const unsigned char junk[]="this is not a wave file";
typedef struct { const char *path; const unsigned char *data; size_t size; } MockFile;
#define D APPLET_DIRECTORY
static const MockFile files[]={
 {D "/settings/a.wav",junk,sizeof junk},{D "/settings/b.wav",wav,sizeof wav},{D "/settings/c.wav",wav,sizeof wav},
 {D "/user/x.wav",junk,sizeof junk},
};
typedef struct { const char *path; const char *entries[8]; } MockDir;
static const MockDir dirs[]={
 {D "/settings",{"c.wav","._hidden.wav","a.wav","notes.txt","b.wav",NULL}},
 {D "/user",{"x.wav",NULL}},
 {D "/album",{NULL}},
};
static const MockDir *open_dir;
static char config[4096]; static size_t config_size; static bool have_config;
static char log_data[4096]; static size_t log_size; static bool have_log; static unsigned log_writes;
static unsigned opens,closes;
enum { KindConfig=100, KindLog=101 };

Result fsFsOpenFile(FsFileSystem *sd,const char *path,unsigned mode,FsFile *file) {
 (void)sd;
 if (!strcmp(path,APPLET_CONFIG)) { if (!have_config) return 0x202; assert(mode==FsOpenMode_Read); file->kind=KindConfig; }
 else if (!strcmp(path,APPLET_LOG)) { if (!have_log) return 0x202; assert(mode==FsOpenMode_Write); file->kind=KindLog; }
 else {
  unsigned i=0; while (i<sizeof files/sizeof *files && strcmp(files[i].path,path)) ++i;
  if (i==sizeof files/sizeof *files) return 0x202;
  assert(mode==FsOpenMode_Read); file->kind=i+1;
 }
 opens++; return 0;
}
static size_t size_of(const FsFile *f) { return f->kind==KindConfig ? config_size : f->kind==KindLog ? log_size : files[f->kind-1].size; }
Result fsFileGetSize(FsFile *f,s64 *size) { *size=(s64)size_of(f); return 0; }
Result fsFileRead(FsFile *f,s64 offset,void *out,u64 length,unsigned option,u64 *got) {
 assert(!option&&offset>=0);
 if ((u64)offset>size_of(f)||length>size_of(f)-(u64)offset) return 0x202;
 const void *source=f->kind==KindConfig ? (const void*)config : (const void*)files[f->kind-1].data;
 memcpy(out,(const unsigned char*)source+offset,length); *got=length; return 0;
}
void fsFileClose(FsFile *f) { (void)f; closes++; }
Result fsFileSetSize(FsFile *f,s64 size) { assert(f->kind==KindLog&&size>=0&&(size_t)size<=sizeof log_data); log_size=(size_t)size; return 0; }
Result fsFileWrite(FsFile *f,s64 off,const void *input,u64 length,unsigned option) {
 assert(f->kind==KindLog&&option==FsWriteOption_Flush&&off>=0&&(u64)off+length==log_size);
 memcpy(log_data+off,input,length); log_writes++; return 0;
}
Result fsFsOpenDirectory(FsFileSystem *sd,const char *path,unsigned mode,FsDir *dir) {
 (void)sd; assert(mode==FsDirOpenMode_ReadFiles);
 for (unsigned i=0;i<sizeof dirs/sizeof *dirs;i++) if (!strcmp(dirs[i].path,path)) { open_dir=&dirs[i]; dir->cursor=0; return 0; }
 return 0x202;
}
Result fsDirRead(FsDir *dir,s64 *count,s64 capacity,FsDirectoryEntry *entry) {
 assert(capacity==1);
 const char *name=open_dir->entries[dir->cursor];
 if (!name) { *count=0; return 0; }
 dir->cursor++; strcpy(entry->name,name); *count=1; return 0;
}
void fsDirClose(FsDir *dir) { (void)dir; }

// ---- helpers --------------------------------------------------------------------------------------
enum { Settings=CONTEXT_APPLET+0,User,News,GameShare,Album,Controller };
enum { Applet=0,Power=2,OperationMode=3,Initialize=4 };
enum { Launch=0,Exit=1,InFocus=2,OutOfFocus=3 };
enum { AppHome=3,AppGame=1,AppAlbum=0x15,AppController=0x0C,AppUser=0x1A,AppOther=0x30 };
static Focus focus;
static void feed(unsigned kind,unsigned applet,unsigned event) { focus_update(&focus,kind,applet,0,event); }
static void feed_program(unsigned applet,uint64_t program,unsigned event) { focus_update(&focus,Applet,applet,program,event); }
static int where(const AppletRule *rules) { return focus_context(&focus,rules); }

int main(void) {
 AppletRule rules[APPLET_RULES]; applet_rules_default(rules);

 // Folder layout, as literal paths so an edit to the macros cannot quietly move things:
 // /switch/kai-menu-music/{home,settings,user,news,gameshare,album,controller}, plus the NRO and two optional files.
 assert(!strcmp(MUSIC_ROOT,"/switch/kai-menu-music")&&!strcmp(APPLET_DIRECTORY,"/switch/kai-menu-music"));
 assert(!strcmp(MUSIC_DIRECTORY,"/switch/kai-menu-music/home"));
 assert(!strcmp(MUSIC_SELECTION,"/switch/kai-menu-music/home/selected.txt"));
 assert(!strcmp(MUSIC_SHUFFLE,"/switch/kai-menu-music/home/shuffle.txt"));
 assert(!strcmp(APPLET_CONFIG,"/switch/kai-menu-music/applets.txt"));
 assert(!strcmp(APPLET_LOG,"/switch/kai-menu-music/applet-log.txt"));
 for (unsigned i=0;i<APPLET_RULES;i++) assert(strcmp(rules[i].folder,"home")!=0); // HOME keeps its own folder

 // Defaults: the six pages with the ids seen on a Switch 2. Program ids are not used (every page reports HOME's).
 static const struct { const char *folder; unsigned applet; } expected[APPLET_RULES]={
  {"settings",0x16},{"user",0x1A},{"news",0x23},{"gameshare",0x24},{"album",0x15},{"controller",0x0C}};
 for (unsigned i=0;i<APPLET_RULES;i++)
  assert(!strcmp(rules[i].folder,expected[i].folder)&&rules[i].enabled&&rules[i].applet==expected[i].applet&&!rules[i].program);

 // Rule parsing.
 static const char text[]="# comment\nSettings = 0x17\r\nnews=20 0100000000001000  # trailing\n gameshare\t=\t1f\n"
  "bogus = 5\nalbum = zz\nuser = 100\ncontroller = 0c ffffffffffffffffff\n";
 applet_rules_parse(rules,text,sizeof text-1);
 assert(rules[0].enabled&&rules[0].applet==0x17&&!rules[0].program);
 assert(rules[2].enabled&&rules[2].applet==0x20&&rules[2].program==0x0100000000001000ULL);
 assert(rules[3].enabled&&rules[3].applet==0x1F);
 assert(rules[4].applet==0x15);                       // "zz" is not a number
 assert(rules[1].applet==0x1A);                       // 0x100 does not fit a PDM applet id
 assert(rules[5].applet==0x0C&&!rules[5].program);    // 18 digits is not a program id
 applet_rules_default(rules);
 char longline[300]; memset(longline,'a',sizeof longline); memcpy(longline+250,"\nalbum = 17\n",12);
 applet_rules_parse(rules,longline,262);
 assert(rules[4].applet==0x17);                       // an overlong line is skipped, the next one still applies
 applet_rules_default(rules);
 applet_rules_parse(rules,"album = 18\0junk\n",16); assert(rules[4].applet==0x15);
 applet_rules_parse(rules,"album = 19",10); assert(rules[4].applet==0x19); // no final newline
 applet_rules_parse(NULL,"album = 1\n",10); applet_rules_parse(rules,NULL,10);
 applet_rules_default(rules);

 // Program ids arrive with their halves swapped.
 assert(program_id_make(0x01000000,0x00001000)==0x0100000000001000ULL);

 // HOME on its own behaves as before.
 memset(&focus,0,sizeof focus);
 feed(Applet,AppHome,InFocus); assert(where(rules)==CONTEXT_HOME);
 feed(Applet,AppHome,OutOfFocus); assert(where(rules)==CONTEXT_NONE);

 // A page opened from HOME gets its own context, in either event order, and HOME returns on close.
 memset(&focus,0,sizeof focus);
 feed(Applet,AppHome,InFocus); feed(Applet,AppHome,OutOfFocus); feed(Applet,AppAlbum,InFocus);
 assert(where(rules)==Album);
 feed(Applet,AppAlbum,Exit); assert(where(rules)==CONTEXT_NONE);
 feed(Applet,AppHome,InFocus); assert(where(rules)==CONTEXT_HOME);
 memset(&focus,0,sizeof focus);
 feed(Applet,AppHome,InFocus); feed(Applet,AppAlbum,InFocus); feed(Applet,AppHome,OutOfFocus);
 assert(where(rules)==Album);
 feed(Applet,AppHome,InFocus); feed(Applet,AppAlbum,Exit); assert(where(rules)==CONTEXT_HOME);

 // Each page is its own context.
 memset(&focus,0,sizeof focus); feed(Applet,AppHome,InFocus);
 feed(Applet,0x16,InFocus); assert(where(rules)==Settings);
 feed(Applet,0x23,InFocus); assert(where(rules)==News);
 feed(Applet,0x24,InFocus); assert(where(rules)==GameShare);
 feed(Applet,AppUser,InFocus); assert(where(rules)==User);
 feed(Applet,AppController,InFocus); assert(where(rules)==Controller);
 feed(Applet,AppOther,InFocus); assert(where(rules)==CONTEXT_NONE);   // an applet with no rule is silent
 feed(Applet,AppAlbum,InFocus); assert(where(rules)==Album);          // and a later page still works

 // Another applet closing does not end the page in front; launches alone prove nothing.
 memset(&focus,0,sizeof focus); feed(Applet,AppHome,InFocus); feed(Applet,AppAlbum,InFocus);
 feed(Applet,AppController,OutOfFocus); assert(where(rules)==Album);
 memset(&focus,0,sizeof focus); feed(Applet,AppHome,InFocus); feed(Applet,AppAlbum,Launch);
 assert(where(rules)==CONTEXT_HOME);

 // A game's own controller prompt must stay silent; the same applet from HOME plays.
 memset(&focus,0,sizeof focus);
 feed(Applet,AppHome,InFocus); feed(Applet,AppGame,InFocus); assert(where(rules)==CONTEXT_NONE);
 feed(Applet,AppController,InFocus); assert(where(rules)==CONTEXT_NONE);
 feed(Applet,AppController,Exit); feed(Applet,AppGame,InFocus);
 feed(Applet,AppHome,InFocus); feed(Applet,AppController,InFocus); assert(where(rules)==Controller);
 // History that starts mid-game, with no HOME event, stays silent.
 memset(&focus,0,sizeof focus); feed(Applet,AppController,InFocus); assert(where(rules)==CONTEXT_NONE);

 // Sleep or a log restart forgets everything; docking does not.
 memset(&focus,0,sizeof focus); feed(Applet,AppHome,InFocus); feed(Applet,AppAlbum,InFocus);
 feed(OperationMode,0,0); assert(where(rules)==Album);
 feed(Power,0,0); assert(where(rules)==CONTEXT_NONE);
 feed(Applet,AppAlbum,InFocus); assert(where(rules)==CONTEXT_NONE);
 feed(Applet,AppHome,InFocus); feed(Initialize,0,0); assert(where(rules)==CONTEXT_NONE);

 // Disabled rules never match; configuring one turns it on.
 memset(&focus,0,sizeof focus); feed(Applet,AppHome,InFocus); rules[0].enabled=false;
 feed(Applet,0x16,InFocus); assert(where(rules)==CONTEXT_NONE);
 static const char more[]="settings = 16\nnews = 20 0100000000001000\n";
 applet_rules_parse(rules,more,sizeof more-1);
 feed(Applet,0x16,InFocus); assert(where(rules)==Settings);
 feed_program(0x20,0x0100000000001000ULL,InFocus); assert(where(rules)==News);
 feed_program(0x20,0x0100000000002000ULL,InFocus); assert(where(rules)==CONTEXT_NONE);
 applet_rules_default(rules);

 // The exact pattern a Switch 2 logs (applet-log.txt): every page shares HOME's program id and is
 // launched, takes focus after HOME loses it, gives it up (event 4) and exits before HOME returns.
 {
  const uint64_t qlaunch=0x0400000000001000ULL,game=0x0400AAA021794000ULL;
  static const struct { unsigned applet; int context; } pages[]={
   {0x16,Settings},{0x23,News},{0x24,GameShare},{0x15,Album},{0x0C,Controller},{0x1A,User}};
  memset(&focus,0,sizeof focus);
  feed_program(AppGame,game,Exit); feed_program(AppGame,game,Launch);          // a game starting up
  feed_program(AppHome,qlaunch,4); feed_program(AppGame,game,InFocus);
  feed_program(AppGame,game,4); feed_program(AppGame,game,InFocus);
  assert(where(rules)==CONTEXT_NONE);
  feed_program(AppGame,game,4); feed_program(AppHome,qlaunch,InFocus); feed_program(AppGame,game,Exit);
  assert(where(rules)==CONTEXT_HOME);                                          // HOME button
  for (unsigned i=0;i<sizeof pages/sizeof *pages;i++) {
   feed_program(pages[i].applet,qlaunch,Launch);
   assert(where(rules)==CONTEXT_HOME);                                         // launching alone changes nothing
   feed_program(AppHome,qlaunch,OutOfFocus); assert(where(rules)==CONTEXT_NONE);
   feed_program(pages[i].applet,qlaunch,InFocus); assert(where(rules)==pages[i].context);
   feed_program(pages[i].applet,qlaunch,4); assert(where(rules)==CONTEXT_NONE); // music stops the moment it loses focus
   feed_program(pages[i].applet,qlaunch,Exit); assert(where(rules)==CONTEXT_NONE);
   feed_program(AppHome,qlaunch,InFocus); assert(where(rules)==CONTEXT_HOME);
  }
  // The error applet and the unknown applet 5 that HOME launches are not pages.
  feed_program(AppHome,qlaunch,4); feed_program(0x0E,0x040000000000100CULL,Launch);
  feed_program(0x0E,0x040000000000100CULL,InFocus); assert(where(rules)==CONTEXT_NONE);
  feed_program(0x0E,0x040000000000100CULL,4); feed_program(0x0E,0x040000000000100CULL,Exit);
  feed_program(AppHome,qlaunch,InFocus); assert(where(rules)==CONTEXT_HOME);
 }

 // Finding a page's song: first valid WAV in filename order, ignoring junk, hidden and non-WAV files.
 FsFileSystem sd={0}; char path[FS_MAX_PATH];
 assert(!applet_find_track(&sd,"settings",path)&&!strcmp(path,D "/settings/b.wav"));
 assert(!strcmp(path,"/switch/kai-menu-music/settings/b.wav"));
 assert(applet_find_track(&sd,"user",path)!=0);       // only an invalid WAV
 assert(applet_find_track(&sd,"album",path)!=0);      // empty folder
 assert(applet_find_track(&sd,"news",path)!=0);       // no folder

 // Rules from the SD card.
 assert(applet_rules_read(&sd,rules)!=0&&rules[4].applet==0x15);   // no file: defaults stay
 have_config=true; strcpy(config,"album = 21\n"); config_size=strlen(config);
 assert(!applet_rules_read(&sd,rules)&&rules[4].applet==0x21);
 applet_rules_default(rules); config_size=3000;
 assert(applet_rules_read(&sd,rules)!=0&&rules[4].applet==0x15);   // too large to be ours
 config_size=0; assert(applet_rules_read(&sd,rules)!=0);

 // Discovery log: only when the file exists, one line per event, capped.
 char line[96];
 assert(applet_log_format(line,sizeof line,12,0x15,0x010000000000100DULL,InFocus)>0);
 assert(!strcmp(line,"#12 applet=0x15 program=010000000000100D event=2 in_focus\n"));
 assert(applet_log_format(line,sizeof line,1,3,0,9)>0&&strstr(line,"unknown"));
 assert(applet_log_format(line,8,12,0x15,0x010000000000100DULL,InFocus)==0);
 assert(applet_log_append(&sd,"a\n",2)!=0&&!log_writes);
 have_log=true;
 assert(!applet_log_append(&sd,"one\n",4)&&!applet_log_append(&sd,"two\n",4));
 assert(log_size==8&&!memcmp(log_data,"one\ntwo\n",8)&&log_writes==2);
 log_size=APPLET_LOG_LIMIT-2; assert(!applet_log_append(&sd,"over\n",5)&&log_writes==2);
 assert(opens==closes);
 puts("Applet rules, focus tracking, folder lookup and discovery log checks passed");
}
