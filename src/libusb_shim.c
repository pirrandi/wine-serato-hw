/*
 * libusb-1.0.dll replacement for Windows applications running under Wine.
 *
 * Wine's USB stack (wineusb) only exposes devices that are not claimed by a
 * Linux kernel driver, so USB audio/MIDI controllers bound to snd-usb-audio
 * are invisible to libusb's Windows backend. Applications that discover
 * hardware by enumerating USB devices (e.g. Serato DJ) therefore never see
 * the controller.
 *
 * This library implements the read-only enumeration part of the libusb-1.0
 * API on top of Linux sysfs (/sys/bus/usb/devices), read through Wine's Z:
 * drive. Device I/O (libusb_open and transfers) is not supported: the
 * controller keeps being driven by Linux (ALSA), and Wine exposes it to the
 * application through WinMM/WASAPI as usual.
 *
 * It is an independent implementation of the documented libusb API; it does
 * not contain libusb code. Replacing the libusb-1.0.dll shipped with an
 * application is permitted by libusb's LGPL license.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libusb_abi.h"

#define API __declspec(dllexport) LIBUSB_CALL

#define SYSFS_USB_DEVICES "Z:\\sys\\bus\\usb\\devices"
#define MAX_DESCRIPTORS_SIZE 65536

#define DT_CONFIG    0x02
#define DT_INTERFACE 0x04
#define DT_ENDPOINT  0x05

struct libusb_context {
    int unused;
};

struct libusb_device {
    LONG refcount;
    char name[64];           /* sysfs name, e.g. "1-3" or "usb1" */
    uint8_t bus_number;
    uint8_t device_address;
    uint8_t port_number;
    int speed;
    unsigned char *descriptors; /* device descriptor followed by configs */
    DWORD descriptors_len;
};

static BOOL log_enabled;

static void log_msg(const char *fmt, ...)
{
    va_list args;
    if (!log_enabled) return;
    fputs("[wine-serato-hw:libusb] ", stderr);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

/* Reads a sysfs attribute of a USB device; returns the number of bytes read. */
static DWORD read_attr(const char *dev, const char *attr, void *buf, DWORD size)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD total = 0, got;

    snprintf(path, sizeof(path), SYSFS_USB_DEVICES "\\%s\\%s", dev, attr);
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    while (total < size && ReadFile(file, (char *)buf + total, size - total, &got, NULL) && got)
        total += got;
    CloseHandle(file);
    return total;
}

static int read_int_attr(const char *dev, const char *attr, int fallback)
{
    char text[32];
    DWORD len = read_attr(dev, attr, text, sizeof(text) - 1);
    if (!len) return fallback;
    text[len] = 0;
    return atoi(text);
}

static int speed_from_mbps(int mbps)
{
    switch (mbps) {
    case 1: return LIBUSB_SPEED_LOW;     /* sysfs reports "1.5" */
    case 12: return LIBUSB_SPEED_FULL;
    case 480: return LIBUSB_SPEED_HIGH;
    case 5000: return LIBUSB_SPEED_SUPER;
    case 10000: case 20000: return LIBUSB_SPEED_SUPER_PLUS;
    default: return LIBUSB_SPEED_UNKNOWN;
    }
}

/* "1-3" -> 3, "3-3.2" -> 2, "usb1" (root hub) -> 0 */
static uint8_t port_from_name(const char *name)
{
    const char *sep = strrchr(name, '.');
    if (!sep) sep = strrchr(name, '-');
    return sep ? (uint8_t)atoi(sep + 1) : 0;
}

static libusb_device *load_device(const char *name)
{
    libusb_device *dev;
    unsigned char *buf;
    DWORD len;

    buf = malloc(MAX_DESCRIPTORS_SIZE);
    if (!buf) return NULL;
    len = read_attr(name, "descriptors", buf, MAX_DESCRIPTORS_SIZE);
    if (len < sizeof(struct libusb_device_descriptor) || buf[1] != 0x01) {
        free(buf);
        return NULL;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        free(buf);
        return NULL;
    }
    dev->refcount = 1;
    lstrcpynA(dev->name, name, sizeof(dev->name));
    dev->bus_number = (uint8_t)read_int_attr(name, "busnum", 0);
    dev->device_address = (uint8_t)read_int_attr(name, "devnum", 0);
    dev->port_number = port_from_name(name);
    dev->speed = speed_from_mbps(read_int_attr(name, "speed", 0));
    dev->descriptors = buf;
    dev->descriptors_len = len;
    if ((buf = realloc(buf, len))) dev->descriptors = buf;
    return dev;
}

static void destroy_device(libusb_device *dev)
{
    free(dev->descriptors);
    free(dev);
}

/* ---- context ---------------------------------------------------------- */

int API libusb_init(libusb_context **ctx)
{
    if (ctx) {
        *ctx = calloc(1, sizeof(**ctx));
        if (!*ctx) return LIBUSB_ERROR_NO_MEM;
    }
    log_msg("libusb_init (sysfs backend)");
    return LIBUSB_SUCCESS;
}

void API libusb_exit(libusb_context *ctx)
{
    free(ctx);
}

void API libusb_set_debug(libusb_context *ctx, int level)
{
    (void)ctx; (void)level;
}

int __declspec(dllexport) __cdecl libusb_set_option(libusb_context *ctx, int option, ...)
{
    (void)ctx; (void)option;
    return LIBUSB_SUCCESS;
}

const struct libusb_version *API libusb_get_version(void)
{
    static const struct libusb_version version = {
        1, 0, 23, 0, "", "wine-serato-hw sysfs backend"
    };
    return &version;
}

int API libusb_has_capability(uint32_t capability)
{
    (void)capability;
    return 0;
}

const char *API libusb_error_name(int code)
{
    switch (code) {
    case LIBUSB_SUCCESS: return "LIBUSB_SUCCESS";
    case LIBUSB_ERROR_IO: return "LIBUSB_ERROR_IO";
    case LIBUSB_ERROR_INVALID_PARAM: return "LIBUSB_ERROR_INVALID_PARAM";
    case LIBUSB_ERROR_ACCESS: return "LIBUSB_ERROR_ACCESS";
    case LIBUSB_ERROR_NO_DEVICE: return "LIBUSB_ERROR_NO_DEVICE";
    case LIBUSB_ERROR_NOT_FOUND: return "LIBUSB_ERROR_NOT_FOUND";
    case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
    case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
    case LIBUSB_ERROR_OVERFLOW: return "LIBUSB_ERROR_OVERFLOW";
    case LIBUSB_ERROR_PIPE: return "LIBUSB_ERROR_PIPE";
    case LIBUSB_ERROR_INTERRUPTED: return "LIBUSB_ERROR_INTERRUPTED";
    case LIBUSB_ERROR_NO_MEM: return "LIBUSB_ERROR_NO_MEM";
    case LIBUSB_ERROR_NOT_SUPPORTED: return "LIBUSB_ERROR_NOT_SUPPORTED";
    default: return "LIBUSB_ERROR_OTHER";
    }
}

const char *API libusb_strerror(int code)
{
    return libusb_error_name(code);
}

/* ---- device list ------------------------------------------------------ */

ssize_t API libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
    static LONG last_count = -1;
    WIN32_FIND_DATAA entry;
    libusb_device **devs = NULL, **grown;
    size_t count = 0, capacity = 0;
    HANDLE find;

    (void)ctx;
    if (!list) return LIBUSB_ERROR_INVALID_PARAM;

    find = FindFirstFileA(SYSFS_USB_DEVICES "\\*", &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            libusb_device *dev;
            /* Skip "." / ".." and interface nodes such as "1-3:1.0". */
            if (entry.cFileName[0] == '.' || strchr(entry.cFileName, ':')) continue;
            if (!(dev = load_device(entry.cFileName))) continue;
            if (count + 1 >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                grown = realloc(devs, capacity * sizeof(*devs));
                if (!grown) {
                    destroy_device(dev);
                    break;
                }
                devs = grown;
            }
            devs[count++] = dev;
        } while (FindNextFileA(find, &entry));
        FindClose(find);
    }

    if (!devs && !(devs = malloc(sizeof(*devs)))) return LIBUSB_ERROR_NO_MEM;
    devs[count] = NULL;
    *list = devs;

    if (InterlockedExchange(&last_count, (LONG)count) != (LONG)count) {
        log_msg("enumerated %u USB devices from sysfs", (unsigned)count);
        for (size_t i = 0; i < count; i++) {
            const struct libusb_device_descriptor *d = (const void *)devs[i]->descriptors;
            log_msg("  %-8s bus %u addr %u  %04x:%04x bcd %04x", devs[i]->name,
                    devs[i]->bus_number, devs[i]->device_address,
                    d->idVendor, d->idProduct, d->bcdDevice);
        }
    }
    return (ssize_t)count;
}

libusb_device *API libusb_ref_device(libusb_device *dev)
{
    if (dev) InterlockedIncrement(&dev->refcount);
    return dev;
}

void API libusb_unref_device(libusb_device *dev)
{
    if (dev && InterlockedDecrement(&dev->refcount) == 0) destroy_device(dev);
}

void API libusb_free_device_list(libusb_device **list, int unref_devices)
{
    if (!list) return;
    if (unref_devices)
        for (libusb_device **dev = list; *dev; dev++) libusb_unref_device(*dev);
    free(list);
}

uint8_t API libusb_get_bus_number(libusb_device *dev)
{
    return dev->bus_number;
}

uint8_t API libusb_get_device_address(libusb_device *dev)
{
    return dev->device_address;
}

uint8_t API libusb_get_port_number(libusb_device *dev)
{
    return dev->port_number;
}

int API libusb_get_device_speed(libusb_device *dev)
{
    return dev->speed;
}

/* ---- descriptors ------------------------------------------------------ */

int API libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{
    if (!dev || !desc) return LIBUSB_ERROR_INVALID_PARAM;
    /* The wire format is little-endian and matches the struct layout. */
    memcpy(desc, dev->descriptors, sizeof(*desc));
    return LIBUSB_SUCCESS;
}

/* Finds the raw bytes of the config descriptor at the given index. */
static int find_raw_config(libusb_device *dev, uint8_t index, const unsigned char **raw, DWORD *len)
{
    DWORD offset = dev->descriptors[0];
    for (uint8_t i = 0; offset + 9 <= dev->descriptors_len; i++) {
        const unsigned char *cfg = dev->descriptors + offset;
        DWORD total = cfg[2] | (cfg[3] << 8);
        if (cfg[1] != DT_CONFIG || total < 9) break;
        if (total > dev->descriptors_len - offset) total = dev->descriptors_len - offset;
        if (i == index) {
            *raw = cfg;
            *len = total;
            return LIBUSB_SUCCESS;
        }
        offset += total;
    }
    return LIBUSB_ERROR_NOT_FOUND;
}

/* Returns the offset just past the run of "extra" (class/vendor) descriptors. */
static DWORD skip_extra(const unsigned char *raw, DWORD len, DWORD pos)
{
    while (pos + 2 <= len && raw[pos] >= 2 && pos + raw[pos] <= len &&
           raw[pos + 1] != DT_INTERFACE && raw[pos + 1] != DT_ENDPOINT)
        pos += raw[pos];
    return pos;
}

static BOOL valid_desc(const unsigned char *raw, DWORD len, DWORD pos)
{
    return pos + 2 <= len && raw[pos] >= 2 && pos + raw[pos] <= len;
}

/*
 * Parses a raw configuration into libusb's nested structures. Everything lives
 * in a single allocation so libusb_free_config_descriptor is a plain free().
 */
static struct libusb_config_descriptor *parse_config(const unsigned char *src, DWORD len)
{
    DWORD n_if = 0, n_ep = 0, n_groups = 0, pos;
    int group_of_number[256];
    uint8_t group_numbers[256];
    struct libusb_config_descriptor *cfg;
    struct libusb_interface *groups;
    struct libusb_interface_descriptor *alts;
    struct libusb_endpoint_descriptor *eps;
    unsigned char *raw;
    size_t size;

    memset(group_of_number, -1, sizeof(group_of_number));
    for (pos = src[0]; valid_desc(src, len, pos); pos += src[pos]) {
        if (src[pos + 1] == DT_INTERFACE && src[pos] >= 9) {
            n_if++;
            if (group_of_number[src[pos + 2]] < 0) {
                group_of_number[src[pos + 2]] = (int)n_groups;
                group_numbers[n_groups++] = src[pos + 2];
            }
        } else if (src[pos + 1] == DT_ENDPOINT && src[pos] >= 7) {
            n_ep++;
        }
    }

    size = sizeof(*cfg) + n_groups * sizeof(*groups) + n_if * sizeof(*alts) +
           n_ep * sizeof(*eps) + len;
    if (!(cfg = calloc(1, size))) return NULL;
    groups = (void *)(cfg + 1);
    alts = (void *)(groups + n_groups);
    eps = (void *)(alts + n_if);
    raw = (unsigned char *)(eps + n_ep);
    memcpy(raw, src, len);

    cfg->bLength = raw[0];
    cfg->bDescriptorType = raw[1];
    cfg->wTotalLength = raw[2] | (raw[3] << 8);
    cfg->bNumInterfaces = (uint8_t)n_groups;
    cfg->bConfigurationValue = raw[5];
    cfg->iConfiguration = raw[6];
    cfg->bmAttributes = raw[7];
    cfg->MaxPower = raw[8];
    cfg->interface = groups;
    pos = skip_extra(raw, len, raw[0]);
    cfg->extra = raw[0] < pos ? raw + raw[0] : NULL;
    cfg->extra_length = (int)(pos - raw[0]);

    /* Alternate settings are stored contiguously per interface number. */
    for (DWORD g = 0; g < n_groups; g++) {
        groups[g].altsetting = alts;
        for (pos = raw[0]; valid_desc(raw, len, pos); pos += raw[pos]) {
            struct libusb_interface_descriptor *alt = alts;
            DWORD extra_start, p;
            uint8_t found = 0;

            if (raw[pos + 1] != DT_INTERFACE || raw[pos] < 9 || raw[pos + 2] != group_numbers[g])
                continue;
            alt->bLength = raw[pos];
            alt->bDescriptorType = raw[pos + 1];
            alt->bInterfaceNumber = raw[pos + 2];
            alt->bAlternateSetting = raw[pos + 3];
            alt->bInterfaceClass = raw[pos + 5];
            alt->bInterfaceSubClass = raw[pos + 6];
            alt->bInterfaceProtocol = raw[pos + 7];
            alt->iInterface = raw[pos + 8];
            alt->endpoint = eps;

            extra_start = pos + raw[pos];
            p = skip_extra(raw, len, extra_start);
            alt->extra = p > extra_start ? raw + extra_start : NULL;
            alt->extra_length = (int)(p - extra_start);

            while (valid_desc(raw, len, p) && raw[p + 1] != DT_INTERFACE) {
                if (raw[p + 1] == DT_ENDPOINT && raw[p] >= 7) {
                    struct libusb_endpoint_descriptor *ep = eps++;
                    DWORD ep_extra = p + raw[p], ep_end;
                    ep->bLength = raw[p];
                    ep->bDescriptorType = raw[p + 1];
                    ep->bEndpointAddress = raw[p + 2];
                    ep->bmAttributes = raw[p + 3];
                    ep->wMaxPacketSize = raw[p + 4] | (raw[p + 5] << 8);
                    ep->bInterval = raw[p + 6];
                    if (raw[p] >= 9) {
                        ep->bRefresh = raw[p + 7];
                        ep->bSynchAddress = raw[p + 8];
                    }
                    ep_end = skip_extra(raw, len, ep_extra);
                    ep->extra = ep_end > ep_extra ? raw + ep_extra : NULL;
                    ep->extra_length = (int)(ep_end - ep_extra);
                    found++;
                    p = ep_end;
                } else {
                    p += raw[p];
                }
            }
            alt->bNumEndpoints = found;
            if (!found) alt->endpoint = NULL;
            alts++;
            groups[g].num_altsetting++;
        }
    }
    return cfg;
}

int API libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index,
                                     struct libusb_config_descriptor **config)
{
    const unsigned char *raw;
    DWORD len;
    int ret;

    if (!dev || !config) return LIBUSB_ERROR_INVALID_PARAM;
    if ((ret = find_raw_config(dev, config_index, &raw, &len)) != LIBUSB_SUCCESS) return ret;
    if (!(*config = parse_config(raw, len))) return LIBUSB_ERROR_NO_MEM;
    return LIBUSB_SUCCESS;
}

int API libusb_get_active_config_descriptor(libusb_device *dev,
                                            struct libusb_config_descriptor **config)
{
    /* Controllers of interest have a single configuration. */
    return libusb_get_config_descriptor(dev, 0, config);
}

void API libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{
    free(config);
}

/* ---- device I/O is not available -------------------------------------- */

int API libusb_open(libusb_device *dev, libusb_device_handle **handle)
{
    (void)dev;
    if (handle) *handle = NULL;
    return LIBUSB_ERROR_NOT_SUPPORTED;
}

void API libusb_close(libusb_device_handle *handle)
{
    (void)handle;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char value[8];
        DisableThreadLibraryCalls(instance);
        log_enabled = GetEnvironmentVariableA("WINE_SERATO_HW_LOG", value, sizeof(value)) > 0;
    }
    return TRUE;
}
