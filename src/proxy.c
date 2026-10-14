/*
 * winmm.dll proxy for Wine.
 *
 * Forwards every export to Wine's builtin winmm.dll, but answers
 * DRV_QUERYDEVICEINTERFACESIZE / DRV_QUERYDEVICEINTERFACE for MIDI devices
 * backed by a USB ALSA card. Wine's ALSA driver returns MMSYSERR_NOTSUPPORTED
 * for these, so Windows applications that identify hardware by USB VID/PID
 * (e.g. Serato DJ) cannot recognize the controller.
 *
 * The reported path mimics the Windows format:
 *   \\?\USB#VID_xxxx&PID_xxxx&MI_00#WINE&ALSA&CARDn#{6994ad04-...}\global
 */
/* We define winmm exports ourselves, so they must not be declared dllimport. */
#define _WINMM_
#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <stdio.h>
#include <wchar.h>

#include "exports.h"

void *real_procs[REAL_PROC_COUNT];

static HMODULE real_winmm;

typedef UINT (WINAPI *midi_message_fn)(HANDLE, UINT, DWORD_PTR, DWORD_PTR);
typedef UINT (WINAPI *num_devs_fn)(void);
typedef MMRESULT (WINAPI *get_id_fn)(HANDLE, UINT *);
typedef MMRESULT (WINAPI *in_caps_fn)(UINT_PTR, MIDIINCAPSW *, UINT);
typedef MMRESULT (WINAPI *out_caps_fn)(UINT_PTR, MIDIOUTCAPSW *, UINT);

static midi_message_fn real_midiInMessage, real_midiOutMessage;
static num_devs_fn real_midiInGetNumDevs, real_midiOutGetNumDevs;
static get_id_fn real_midiInGetID, real_midiOutGetID;
static in_caps_fn real_midiInGetDevCapsW;
static out_caps_fn real_midiOutGetDevCapsW;

static BOOL log_enabled;

static void log_msg(const char *fmt, ...)
{
    va_list args;
    if (!log_enabled) return;
    fputs("[wine-serato-hw] ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

/* Reads a small Linux file through Wine's Z: drive mapping. */
static BOOL read_unix_file(const char *unix_path, char *buf, DWORD size)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD read = 0;

    snprintf(path, sizeof(path), "Z:%s", unix_path);
    for (char *p = path; *p; p++) if (*p == '/') *p = '\\';

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(file, buf, size - 1, &read, NULL);
    CloseHandle(file);
    buf[read] = 0;
    return read > 0;
}

/*
 * Finds the ALSA USB card whose name prefixes the MIDI port name.
 * /proc/asound/cards lines look like:
 *   " 3 [D500           ]: USB-Audio - DJControl Inpulse 500"
 */
static BOOL find_usb_card(const char *port_name, int *card, unsigned *vid, unsigned *pid,
                          size_t *name_len)
{
    char cards[8192], usbid[64], path[64];
    char *line, *ctx = NULL;
    size_t best_len = 0;

    if (!read_unix_file("/proc/asound/cards", cards, sizeof(cards))) return FALSE;

    for (line = strtok_s(cards, "\n", &ctx); line; line = strtok_s(NULL, "\n", &ctx))
    {
        int num;
        const char *name = strstr(line, " - ");
        size_t len;

        if (sscanf(line, " %d [", &num) != 1 || !name) continue;
        name += 3;
        len = strcspn(name, "\r\n");
        if (!len || len <= best_len || strncmp(port_name, name, len)) continue;

        snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", num);
        if (!read_unix_file(path, usbid, sizeof(usbid))) continue;
        if (sscanf(usbid, "%x:%x", vid, pid) != 2) continue;

        *card = num;
        best_len = len;
    }
    if (name_len) *name_len = best_len;
    return best_len > 0;
}

static BOOL build_interface_path(BOOL out, UINT id, WCHAR *path, size_t chars)
{
    char name[MAXPNAMELEN * 3];
    WCHAR wname[MAXPNAMELEN];
    unsigned vid, pid;
    int card;

    if (out)
    {
        MIDIOUTCAPSW caps;
        if (real_midiOutGetDevCapsW(id, &caps, sizeof(caps))) return FALSE;
        wcscpy(wname, caps.szPname);
    }
    else
    {
        MIDIINCAPSW caps;
        if (real_midiInGetDevCapsW(id, &caps, sizeof(caps))) return FALSE;
        wcscpy(wname, caps.szPname);
    }
    WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, sizeof(name), NULL, NULL);

    if (!find_usb_card(name, &card, &vid, &pid, NULL))
    {
        log_msg("%s %u '%s': no USB ALSA card", out ? "out" : "in", id, name);
        return FALSE;
    }

    swprintf(path, chars,
             L"\\\\?\\USB#VID_%04X&PID_%04X&MI_00#WINE&ALSA&CARD%d"
             L"#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\\global",
             vid, pid, card);
    log_msg("%s %u '%s' -> %ls", out ? "out" : "in", id, name, path);
    return TRUE;
}

static UINT resolve_device_id(BOOL out, HANDLE handle, UINT *id)
{
    UINT count = out ? real_midiOutGetNumDevs() : real_midiInGetNumDevs();

    if ((UINT_PTR)handle < count)
    {
        *id = (UINT)(UINT_PTR)handle;
        return MMSYSERR_NOERROR;
    }
    return out ? real_midiOutGetID(handle, id) : real_midiInGetID(handle, id);
}

static UINT query_interface(BOOL out, HANDLE handle, UINT msg, DWORD_PTR p1, DWORD_PTR p2)
{
    WCHAR path[256];
    DWORD bytes;
    UINT id;

    if (resolve_device_id(out, handle, &id)) return MMSYSERR_INVALHANDLE;
    if (!build_interface_path(out, id, path, ARRAYSIZE(path))) return MMSYSERR_NOTSUPPORTED;

    bytes = (wcslen(path) + 1) * sizeof(WCHAR);
    if (msg == DRV_QUERYDEVICEINTERFACESIZE)
    {
        if (!p1) return MMSYSERR_INVALPARAM;
        *(DWORD *)p1 = bytes;
        return MMSYSERR_NOERROR;
    }
    if (!p1 || p2 < bytes) return MMSYSERR_INVALPARAM;
    memcpy((void *)p1, path, bytes);
    return MMSYSERR_NOERROR;
}

static BOOL is_interface_query(UINT msg)
{
    return msg == DRV_QUERYDEVICEINTERFACESIZE || msg == DRV_QUERYDEVICEINTERFACE;
}

UINT WINAPI midiInMessage(HMIDIIN handle, UINT msg, DWORD_PTR p1, DWORD_PTR p2)
{
    if (is_interface_query(msg)) return query_interface(FALSE, handle, msg, p1, p2);
    return real_midiInMessage(handle, msg, p1, p2);
}

UINT WINAPI midiOutMessage(HMIDIOUT handle, UINT msg, DWORD_PTR p1, DWORD_PTR p2)
{
    if (is_interface_query(msg)) return query_interface(TRUE, handle, msg, p1, p2);
    return real_midiOutMessage(handle, msg, p1, p2);
}

/*
 * ALSA names a USB MIDI port "<card name> MIDI 1", while Windows uses the
 * device's product name. Applications match controllers by that name, so the
 * single-port suffix is dropped for ports backed by a USB card.
 */
static void fix_port_name(WCHAR *pname)
{
    static const WCHAR suffix[] = L" MIDI 1";
    char name[MAXPNAMELEN * 3];
    unsigned vid, pid;
    size_t card_len;
    int card;

    WideCharToMultiByte(CP_UTF8, 0, pname, -1, name, sizeof(name), NULL, NULL);
    if (!find_usb_card(name, &card, &vid, &pid, &card_len)) return;
    if (strcmp(name + card_len, " MIDI 1")) return;
    pname[wcslen(pname) - ARRAYSIZE(suffix) + 1] = 0;
}

MMRESULT WINAPI midiInGetDevCapsW(UINT_PTR id, MIDIINCAPSW *caps, UINT size)
{
    MIDIINCAPSW full;
    MMRESULT ret;

    if (!caps) return real_midiInGetDevCapsW(id, caps, size);
    if ((ret = real_midiInGetDevCapsW(id, &full, sizeof(full)))) return ret;
    fix_port_name(full.szPname);
    memcpy(caps, &full, min(size, sizeof(full)));
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI midiOutGetDevCapsW(UINT_PTR id, MIDIOUTCAPSW *caps, UINT size)
{
    MIDIOUTCAPSW full;
    MMRESULT ret;

    if (!caps) return real_midiOutGetDevCapsW(id, caps, size);
    if ((ret = real_midiOutGetDevCapsW(id, &full, sizeof(full)))) return ret;
    fix_port_name(full.szPname);
    memcpy(caps, &full, min(size, sizeof(full)));
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI midiInGetDevCapsA(UINT_PTR id, MIDIINCAPSA *caps, UINT size)
{
    MIDIINCAPSW w;
    MIDIINCAPSA a;
    MMRESULT ret;

    if (!caps) return MMSYSERR_INVALPARAM;
    if ((ret = midiInGetDevCapsW(id, &w, sizeof(w)))) return ret;
    a.wMid = w.wMid;
    a.wPid = w.wPid;
    a.vDriverVersion = w.vDriverVersion;
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, a.szPname, sizeof(a.szPname), NULL, NULL);
    a.dwSupport = w.dwSupport;
    memcpy(caps, &a, min(size, sizeof(a)));
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI midiOutGetDevCapsA(UINT_PTR id, MIDIOUTCAPSA *caps, UINT size)
{
    MIDIOUTCAPSW w;
    MIDIOUTCAPSA a;
    MMRESULT ret;

    if (!caps) return MMSYSERR_INVALPARAM;
    if ((ret = midiOutGetDevCapsW(id, &w, sizeof(w)))) return ret;
    a.wMid = w.wMid;
    a.wPid = w.wPid;
    a.vDriverVersion = w.vDriverVersion;
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, a.szPname, sizeof(a.szPname), NULL, NULL);
    a.wTechnology = w.wTechnology;
    a.wVoices = w.wVoices;
    a.wNotes = w.wNotes;
    a.wChannelMask = w.wChannelMask;
    a.dwSupport = w.dwSupport;
    memcpy(caps, &a, min(size, sizeof(a)));
    return MMSYSERR_NOERROR;
}

static BOOL load_real_winmm(HMODULE self)
{
    WCHAR path[MAX_PATH];

    GetSystemDirectoryW(path, MAX_PATH);
    wcscat(path, L"\\winmm.dll");
    real_winmm = LoadLibraryW(path);
    if (!real_winmm || real_winmm == self) return FALSE;

    for (size_t i = 0; i < REAL_PROC_COUNT; i++)
    {
        const char *key = real_exports[i].name
                        ? real_exports[i].name
                        : (const char *)MAKEINTRESOURCEA(real_exports[i].ordinal);
        real_procs[i] = (void *)GetProcAddress(real_winmm, key);
    }

#define RESOLVE(name) (real_##name = (void *)GetProcAddress(real_winmm, #name))
    return RESOLVE(midiInMessage) && RESOLVE(midiOutMessage)
        && RESOLVE(midiInGetNumDevs) && RESOLVE(midiOutGetNumDevs)
        && RESOLVE(midiInGetID) && RESOLVE(midiOutGetID)
        && RESOLVE(midiInGetDevCapsW) && RESOLVE(midiOutGetDevCapsW);
#undef RESOLVE
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        log_enabled = GetEnvironmentVariableA("WINE_SERATO_HW_LOG", NULL, 0) > 0;
        DisableThreadLibraryCalls(instance);
        if (!load_real_winmm(instance))
        {
            fputs("[wine-serato-hw] failed to load builtin winmm.dll\n", stderr);
            return FALSE;
        }
        log_msg("loaded, forwarding %d exports", REAL_PROC_COUNT);
    }
    return TRUE;
}
