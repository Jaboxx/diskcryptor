@echo off
rmdir sys\objfre_wlh_x86 /S /Q
rmdir sys\objfre_wlh_amd64 /S /Q
del   *.ncb /S /Q
del   *.suo /S /Q /F /A:H
del   *.user /S /Q
del   BuildLog.htm /S /Q
del    dcapi.lib  /S /Q
del    dcapi.dll.manifest /S /Q
del    *.exp /S /Q
del    *.dll /S /Q
del    *.pdb /S /Q
del    *.sys  /S /Q
del    *.exe  /S /Q
del    *.log  /S /Q
del    *.obj  /S /Q
del    *.aps  /S /Q
del    *.ilk  /S /Q
del    fast_aes\aes_x86.txt 
del    boot\bin\* /S /Q

rmdir release\amd64\obj /S /Q
rmdir release\i386\obj /S /Q
rmdir release\boot\obj /S /Q

rmdir debug\amd64\obj /S /Q
rmdir debug\i386\obj /S /Q
rmdir debug\boot\obj /S /Q


