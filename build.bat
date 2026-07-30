@echo off
setlocal

set GCC=C:\MinGW\bin\gcc.exe
set GPP=C:\MinGW\bin\g++.exe
set FLAGS=-D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -D_WIN32_IE=0x0600 -Isrc -O2 -Wall
set LIBS=-lgdiplus -lwininet -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 -lole32 -luuid -lcrypt32
set OUT=zabbix-desktop.exe
set BUILD=build

if not exist %BUILD% mkdir %BUILD%

echo === Compiling C files ===

%GCC% %FLAGS% -c src\json.c       -o %BUILD%\json.o       || goto :error
%GCC% %FLAGS% -c src\zabbix_api.c -o %BUILD%\zabbix_api.o || goto :error
%GCC% %FLAGS% -c src\config.c     -o %BUILD%\config.o     || goto :error

echo === Compiling C++ files ===

%GPP% %FLAGS% -c src\render.cpp   -o %BUILD%\render.o     || goto :error

echo === Compiling more C files ===

%GCC% %FLAGS% -c src\widget.c     -o %BUILD%\widget.o     || goto :error
%GCC% %FLAGS% -c src\ui_login.c   -o %BUILD%\ui_login.o   || goto :error
%GCC% %FLAGS% -c src\ui_select.c  -o %BUILD%\ui_select.o  || goto :error
%GCC% %FLAGS% -c src\main.c       -o %BUILD%\main.o       || goto :error

echo === Linking ===

%GPP% -o %OUT% %BUILD%\*.o %LIBS% -mwindows || goto :error

echo === Build successful: %OUT% ===
exit /b 0

:error
echo === Build FAILED ===
exit /b 1
