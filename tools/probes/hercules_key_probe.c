/* Prints what RegQueryInfoKeyW/RegEnumKeyExW return for the Hercules driver key. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    const WCHAR *path = L"SOFTWARE\\WOW6432Node\\Guillemot Corporation\\DJHERCULESMIX Series Drivers";
    HKEY key;
    LONG rc = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    printf("open rc=%ld\n", rc);
    if (rc) return 1;
    WCHAR cls[64]; DWORD cls_len = 64, subkeys, max_sub, max_cls, values, max_vname, max_vdata, sec;
    FILETIME ft;
    rc = RegQueryInfoKeyW(key, cls, &cls_len, NULL, &subkeys, &max_sub, &max_cls, &values,
                          &max_vname, &max_vdata, &sec, &ft);
    printf("info rc=%ld class='%ls' subkeys=%lu max_sub=%lu max_cls=%lu values=%lu\n",
           rc, cls, subkeys, max_sub, max_cls, values);
    for (DWORD i = 0;; i++) {
        WCHAR name[256]; DWORD len = 256;
        rc = RegEnumKeyExW(key, i, name, &len, NULL, NULL, NULL, &ft);
        printf("enum[%lu] rc=%ld name='%ls' len=%lu\n", i, rc, rc ? L"" : name, len);
        if (rc) break;
    }
    RegCloseKey(key);
    return 0;
}
