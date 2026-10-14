/* Minimal ASIO host: instantiates a driver by CLSID and calls init/getChannels/getBufferSize. */
#include <windows.h>
#include <stdio.h>

typedef struct IASIO IASIO;
typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IASIO *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IASIO *);
    ULONG (STDMETHODCALLTYPE *Release)(IASIO *);
    LONG (STDMETHODCALLTYPE *init)(IASIO *, void *);
    void (STDMETHODCALLTYPE *getDriverName)(IASIO *, char *);
    LONG (STDMETHODCALLTYPE *getDriverVersion)(IASIO *);
    void (STDMETHODCALLTYPE *getErrorMessage)(IASIO *, char *);
    LONG (STDMETHODCALLTYPE *start)(IASIO *);
    LONG (STDMETHODCALLTYPE *stop)(IASIO *);
    LONG (STDMETHODCALLTYPE *getChannels)(IASIO *, LONG *, LONG *);
    LONG (STDMETHODCALLTYPE *getLatencies)(IASIO *, LONG *, LONG *);
    LONG (STDMETHODCALLTYPE *getBufferSize)(IASIO *, LONG *, LONG *, LONG *, LONG *);
} IASIOVtbl;
struct IASIO { const IASIOVtbl *lpVtbl; };

int main(void)
{
    static const CLSID clsid = {0x48D0C522, 0xBFCC, 0x45CC, {0x8B, 0x84, 0x17, 0xF2, 0x5F, 0x33, 0xE6, 0xE8}};
    IASIO *asio = NULL;
    char name[64] = "", err[128] = "";
    LONG in = 0, out = 0, mn = 0, mx = 0, pref = 0, gran = 0;

    CoInitialize(NULL);
    HRESULT hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &clsid, (void **)&asio);
    printf("CoCreateInstance hr=%#lx\n", hr); fflush(stdout);
    if (FAILED(hr)) return 1;
    LONG ok = asio->lpVtbl->init(asio, GetDesktopWindow());
    asio->lpVtbl->getDriverName(asio, name);
    asio->lpVtbl->getErrorMessage(asio, err);
    printf("init=%ld name='%s' err='%s'\n", ok, name, err); fflush(stdout);
    if (!ok) return 2;
    LONG rc = asio->lpVtbl->getChannels(asio, &in, &out);
    printf("getChannels=%ld in=%ld out=%ld\n", rc, in, out);
    rc = asio->lpVtbl->getBufferSize(asio, &mn, &mx, &pref, &gran);
    printf("getBufferSize=%ld min=%ld max=%ld pref=%ld gran=%ld\n", rc, mn, mx, pref, gran);
    asio->lpVtbl->Release(asio);
    return 0;
}
