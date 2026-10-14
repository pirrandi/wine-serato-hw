#include <windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <stdio.h>
static void q(const char*tag, UINT (WINAPI*msg)(HANDLE,UINT,DWORD_PTR,DWORD_PTR), HANDLE h){
  DWORD sz=0; WCHAR buf[512]={0}; char a[1024]={0};
  UINT r1=msg(h,DRV_QUERYDEVICEINTERFACESIZE,(DWORD_PTR)&sz,0), r2=999;
  if(sz) { r2=msg(h,DRV_QUERYDEVICEINTERFACE,(DWORD_PTR)buf,sz); WideCharToMultiByte(CP_UTF8,0,buf,-1,a,sizeof a,0,0);}
  printf("   %s: SIZE r=%u sz=%lu IFACE r=%u '%s'\n",tag,r1,sz,r2,a);
}
int main(void){
  UINT i;
  for(i=0;i<midiInGetNumDevs();i++){ MIDIINCAPSW c; char a[64]; HMIDIIN h;
    midiInGetDevCapsW(i,&c,sizeof c); WideCharToMultiByte(CP_UTF8,0,c.szPname,-1,a,64,0,0);
    printf("IN [%u] %s\n",i,a);
    q("by-id ",(void*)midiInMessage,(HANDLE)(UINT_PTR)i);
    if(midiInOpen(&h,i,0,0,CALLBACK_NULL)==0){ q("handle",(void*)midiInMessage,h); midiInClose(h);} }
  for(i=0;i<midiOutGetNumDevs();i++){ MIDIOUTCAPSW c; char a[64]; HMIDIOUT h;
    midiOutGetDevCapsW(i,&c,sizeof c); WideCharToMultiByte(CP_UTF8,0,c.szPname,-1,a,64,0,0);
    printf("OUT[%u] %s\n",i,a);
    q("by-id ",(void*)midiOutMessage,(HANDLE)(UINT_PTR)i);
    if(midiOutOpen(&h,i,0,0,CALLBACK_NULL)==0){ q("handle",(void*)midiOutMessage,h); midiOutClose(h);} }
  return 0;
}
