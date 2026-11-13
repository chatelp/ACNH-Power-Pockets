// Minimal libnx declarations for host-side syntax checking ONLY (tests/host/run.sh).
// Signatures mirror libnx; nothing here is linked or executed.
#pragma once
#include <cstdint>
#include <cstddef>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u32 Result;
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)

typedef struct { u32 h; } FsFileSystem;
typedef struct { u32 h; } FsFile;
typedef struct { u64 application_id; u64 uid[2]; u64 system_save_data_id; u8 save_data_type; u8 save_data_rank; u16 save_data_index; u32 pad; u64 unk[3]; } FsSaveDataAttribute;
typedef enum { FsSaveDataSpaceId_System = 0, FsSaveDataSpaceId_User = 1 } FsSaveDataSpaceId;
enum { FsOpenMode_Read = 1, FsOpenMode_Write = 2, FsOpenMode_Append = 4 };
enum { FsReadOption_None = 0 };
enum { FsWriteOption_None = 0, FsWriteOption_Flush = 1 };

Result fsOpenSaveDataFileSystem(FsFileSystem* out, FsSaveDataSpaceId id, const FsSaveDataAttribute* attr);
Result fsOpenSdCardFileSystem(FsFileSystem* out);
Result fsFsOpenFile(FsFileSystem* fs, const char* path, u32 mode, FsFile* out);
Result fsFsCreateFile(FsFileSystem* fs, const char* path, s64 size, u32 option);
Result fsFsDeleteFile(FsFileSystem* fs, const char* path);
Result fsFsCommit(FsFileSystem* fs);
void   fsFsClose(FsFileSystem* fs);
Result fsFileGetSize(FsFile* f, s64* out);
Result fsFileRead(FsFile* f, s64 off, void* buf, u64 size, u32 option, u64* read);
Result fsFileWrite(FsFile* f, s64 off, const void* buf, u64 size, u32 option);
void   fsFileClose(FsFile* f);

typedef struct { u8 data[0x400]; } SwkbdConfig;
Result swkbdCreate(SwkbdConfig* c, s32 max_dictwords);
void   swkbdConfigMakePresetDefault(SwkbdConfig* c);
void   swkbdConfigSetGuideText(SwkbdConfig* c, const char* s);
void   swkbdConfigSetInitialText(SwkbdConfig* c, const char* s);
Result swkbdShow(SwkbdConfig* c, char* out, size_t size);
void   swkbdClose(SwkbdConfig* c);

struct PrintConsole;
PrintConsole* consoleInit(PrintConsole* c);
void consoleClear(void);
void consoleUpdate(PrintConsole* c);
void consoleExit(PrintConsole* c);

typedef struct { u64 buttons_cur, buttons_old; } PadState;
enum { HidNpadStyleSet_NpadStandard = 0x1F };
void padConfigureInput(u32 max_players, u32 style_set);
void padInitializeDefault(PadState* pad);
void padUpdate(PadState* pad);
u64  padGetButtonsDown(const PadState* pad);
u64  padGetButtons(const PadState* pad);

typedef enum { TimeType_UserSystemClock = 0, TimeType_NetworkSystemClock = 1, TimeType_LocalSystemClock = 2, TimeType_Default = TimeType_UserSystemClock } TimeType;
typedef struct { u16 year; u8 month; u8 day; u8 hour; u8 minute; u8 second; u8 pad; } TimeCalendarTime;
typedef struct { u32 wday; u32 yday; char timezoneName[8]; u32 DST; s32 offset; } TimeCalendarAdditionalInfo;
Result timeGetCurrentTime(TimeType type, u64* timestamp);
Result timeToCalendarTimeWithMyRule(u64 timestamp, TimeCalendarTime* caltime, TimeCalendarAdditionalInfo* info);

typedef enum { AppletType_Application = 0, AppletType_LibraryApplet = 2 } AppletType;
AppletType appletGetAppletType(void);
bool appletMainLoop(void);
void svcSleepThread(s64 ns);

#define BITL(n) (1ULL << (n))
enum : u64 {
    HidNpadButton_A = BITL(0), HidNpadButton_B = BITL(1), HidNpadButton_X = BITL(2), HidNpadButton_Y = BITL(3),
    HidNpadButton_StickL = BITL(4), HidNpadButton_StickR = BITL(5),
    HidNpadButton_L = BITL(6), HidNpadButton_R = BITL(7), HidNpadButton_ZL = BITL(8), HidNpadButton_ZR = BITL(9),
    HidNpadButton_Plus = BITL(10), HidNpadButton_Minus = BITL(11),
    HidNpadButton_Left = BITL(12), HidNpadButton_Up = BITL(13), HidNpadButton_Right = BITL(14), HidNpadButton_Down = BITL(15),
    HidNpadButton_StickLLeft = BITL(16), HidNpadButton_StickLUp = BITL(17), HidNpadButton_StickLRight = BITL(18), HidNpadButton_StickLDown = BITL(19),
    HidNpadButton_StickRLeft = BITL(20), HidNpadButton_StickRUp = BITL(21), HidNpadButton_StickRRight = BITL(22), HidNpadButton_StickRDown = BITL(23),
    HidNpadButton_AnyLeft = HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft,
    HidNpadButton_AnyUp = HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp,
    HidNpadButton_AnyRight = HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight,
    HidNpadButton_AnyDown = HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown,
};
