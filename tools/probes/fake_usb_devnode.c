#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
static const GUID GUID_USB_CLASS = {0x36fc9e60,0xc465,0x11cf,{0x80,0x56,0x44,0x45,0x53,0x54,0x00,0x00}};
static void list(const char *tag){
  HDEVINFO set = SetupDiGetClassDevsW(NULL, L"USB", NULL, DIGCF_PRESENT|DIGCF_ALLCLASSES);
  SP_DEVINFO_DATA d = {sizeof d}; WCHAR id[256]; DWORD i;
  printf("%s:\n", tag);
  for (i=0; SetupDiEnumDeviceInfo(set,i,&d); i++){
    SetupDiGetDeviceInstanceIdW(set,&d,id,256,NULL);
    printf("  %ls\n", id);
  }
  printf("  (%lu present)\n", i);
  SetupDiDestroyDeviceInfoList(set);
}
int main(int argc, char **argv){
  list("before");
  if (argc > 1) {
    HDEVINFO set = SetupDiCreateDeviceInfoList(&GUID_USB_CLASS, NULL);
    SP_DEVINFO_DATA d = {sizeof d};
    BOOL ok = SetupDiCreateDeviceInfoW(set, L"USB\\VID_06F8&PID_B12B\\WINEALSA3", &GUID_USB_CLASS, L"DJControl Inpulse 500", NULL, 0, &d);
    printf("create: %d err=%lu\n", ok, ok?0:GetLastError());
    ok = ok && SetupDiRegisterDeviceInfo(set, &d, 0, NULL, NULL, NULL);
    printf("register: %d err=%lu\n", ok, ok?0:GetLastError());
    SetupDiDestroyDeviceInfoList(set);
  }
  list("after");
  return 0;
}
