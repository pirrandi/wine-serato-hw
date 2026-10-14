/* Lists USB devices through a libusb-1.0.dll, using the same calls as Serato. */
#include <windows.h>
#include <stdio.h>
#include "../../src/libusb_abi.h"

#define LOAD(name) name##_fn name = (name##_fn)(void *)GetProcAddress(lib, #name)

typedef int (LIBUSB_CALL *libusb_init_fn)(libusb_context **);
typedef void (LIBUSB_CALL *libusb_exit_fn)(libusb_context *);
typedef ssize_t (LIBUSB_CALL *libusb_get_device_list_fn)(libusb_context *, libusb_device ***);
typedef void (LIBUSB_CALL *libusb_free_device_list_fn)(libusb_device **, int);
typedef int (LIBUSB_CALL *libusb_get_device_descriptor_fn)(libusb_device *, struct libusb_device_descriptor *);
typedef int (LIBUSB_CALL *libusb_get_config_descriptor_fn)(libusb_device *, uint8_t, struct libusb_config_descriptor **);
typedef void (LIBUSB_CALL *libusb_free_config_descriptor_fn)(struct libusb_config_descriptor *);
typedef uint8_t (LIBUSB_CALL *libusb_get_bus_number_fn)(libusb_device *);
typedef uint8_t (LIBUSB_CALL *libusb_get_device_address_fn)(libusb_device *);

int main(int argc, char **argv)
{
    HMODULE lib = LoadLibraryA(argc > 1 ? argv[1] : "libusb-1.0.dll");
    libusb_context *ctx = NULL;
    libusb_device **list;
    ssize_t n;

    if (!lib) { printf("cannot load library\n"); return 1; }
    LOAD(libusb_init); LOAD(libusb_exit); LOAD(libusb_get_device_list);
    LOAD(libusb_free_device_list); LOAD(libusb_get_device_descriptor);
    LOAD(libusb_get_config_descriptor); LOAD(libusb_free_config_descriptor);
    LOAD(libusb_get_bus_number); LOAD(libusb_get_device_address);

    printf("init: %d\n", libusb_init(&ctx));
    n = libusb_get_device_list(ctx, &list);
    printf("devices: %lld\n", (long long)n);
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor d;
        struct libusb_config_descriptor *c;
        libusb_get_device_descriptor(list[i], &d);
        printf("Vid=0x%x&Pid=0x%x&BcdDevice=0x%x&Instance=%u#%u configs=%u\n",
               d.idVendor, d.idProduct, d.bcdDevice, libusb_get_bus_number(list[i]),
               libusb_get_device_address(list[i]), d.bNumConfigurations);
        if (libusb_get_config_descriptor(list[i], 0, &c) != 0) continue;
        for (int f = 0; f < c->bNumInterfaces; f++)
            for (int a = 0; a < c->interface[f].num_altsetting; a++) {
                const struct libusb_interface_descriptor *alt = &c->interface[f].altsetting[a];
                printf("   if %u alt %u class %02x/%02x eps %u extra %d:", alt->bInterfaceNumber,
                       alt->bAlternateSetting, alt->bInterfaceClass, alt->bInterfaceSubClass,
                       alt->bNumEndpoints, alt->extra_length);
                for (int e = 0; e < alt->bNumEndpoints; e++)
                    printf(" %02x(%u,x%d)", alt->endpoint[e].bEndpointAddress,
                           alt->endpoint[e].wMaxPacketSize, alt->endpoint[e].extra_length);
                printf("\n");
            }
        libusb_free_config_descriptor(c);
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}
