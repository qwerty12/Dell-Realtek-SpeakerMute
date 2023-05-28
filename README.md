# Dell-Realtek-SpeakerMute
On my Windows 11 Dell PC with the Realtek 6.0.8710.1 drivers installed, separate volume states for the built-in speakers and plugged-in headphones are not maintained. Indeed, there is no option anywhere to have them presented as distinct devices (even in Realtek Audio Control).

Dell-Realtek-SpeakerMute is a small program that mutes sound if headphone unplugging is sensed by the jack. RtkAudUService64.exe was partially reversed to find the most quickest and direct way of getting notified of that; as such, Dell-Realtek-SpeakerMute is extremely fast at reacting and muting. Unfortunately, it's probably very specific to my system, so you'll need to build it yourself. To see RtkAudUService64's debug logs, which should give enough information for changes if needed, do the following:

1. `net stop RtkAudioUniversalService`

2. `taskkill /f /im RtkAudUService64.exe`

3. reg add "HKEY_CURRENT_USER\Software\Realtek\Audio\RtkAudUService\General" /f /v DebugFlag /d 1 /t reg_dword

4. [Dbgview.exe](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)

5. `start /D C:\Windows\system32 C:\Windows\system32\RtkAudUService64.exe -background`

---

If you're okay with waiting a second for RtkAudUService64 to update the Registry, then there's a more generic way to detect jack status changes:`

If your program is running as the current user, as long as `C:\Windows\system32\RtkAudUService64.exe -background` is running, `HKEY_CURRENT_USER\Software\Realtek\Audio\RtkAudUService\JackPlug` will be updated.

In a service, `RtkAudUService64.exe` updates `HKEY_LOCAL_MACHINE\SOFTWARE\Realtek\Audio\GUI_INFORMATION\JackInfomation`. However, on my Windows 11 install (clean Windows 10 was okay), this is buggy and doesn't work unless you stop the service, delete `HKEY_LOCAL_MACHINE\SOFTWARE\Realtek` and then start the service again. This will need to be done on every restart.

You can get notified of changes to either with [`RegNotifyChangeKeyValue`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue).