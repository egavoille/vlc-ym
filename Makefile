# Plugin VLC 3.0 pour les fichiers YM (ST-Sound)
# Copyright (C) 2026 Emmanuel Gavoille <egavoille@gmail.com>
# https://lesbidouillesdemonsieurm.com
#
#   Linux  : make
#   Windows (compilation croisée depuis Linux, MinGW-w64) :
#            make win64
#
# Résultat : libym_plugin.so (Linux) ou libym_plugin.dll (Windows)

STSOUND_OBJ = ymdepack.o digidrum.o Ym2149Ex.o Ymload.o YmMusic.o YmUserInterface.o LzhLib.o
STSOUND_SRC = src/ymdepack.cpp StSoundLibrary/digidrum.cpp StSoundLibrary/Ym2149Ex.cpp \
              StSoundLibrary/Ymload.cpp StSoundLibrary/YmMusic.cpp \
              StSoundLibrary/YmUserInterface.cpp StSoundLibrary/LZH/LzhLib.cpp

COMMON_FLAGS = -O2 -fPIC -IStSoundLibrary -DMODULE_STRING=\"ym\" -D__PLUGIN__ \
               -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_THREAD_SAFE
CXXWARN      = -Wno-write-strings -Wno-unused-result -Wno-format-overflow -w

# ---------- Linux / macOS ----------
CC  ?= gcc
CXX ?= g++
VLC_CFLAGS := $(shell pkg-config --cflags vlc-plugin 2>/dev/null)
VLC_LIBS   := $(shell pkg-config --libs vlc-plugin 2>/dev/null)

all: libym_plugin.so

libym_plugin.so: src/ym.c $(STSOUND_SRC)
	$(CC)  $(COMMON_FLAGS) $(VLC_CFLAGS) -std=gnu11 -Wall -c src/ym.c -o ym.o
	$(CXX) $(COMMON_FLAGS) $(CXXWARN) -c $(STSOUND_SRC)
	$(CXX) -shared -o $@ ym.o $(STSOUND_OBJ) -static-libstdc++ -static-libgcc $(VLC_LIBS) \
	       -Wl,--no-undefined -Wl,--exclude-libs,ALL
	rm -f *.o

install: libym_plugin.so
	install -D -m 0644 libym_plugin.so \
	  $(DESTDIR)$(shell pkg-config --variable=pluginsdir vlc-plugin)/demux/libym_plugin.so
	-vlc-cache-gen $(shell pkg-config --variable=pluginsdir vlc-plugin) 2>/dev/null || \
	 /usr/lib/x86_64-linux-gnu/vlc/vlc-cache-gen /usr/lib/x86_64-linux-gnu/vlc/plugins

# ---------- Windows 64 bits (MinGW-w64) ----------
# Utilise les en-têtes officiels de VLC 3.0 (win/vlc-3.0-include) et une
# bibliothèque d'import générée depuis win/libvlccore.def : le SDK Windows
# complet n'est donc pas nécessaire.
MINGW       ?= x86_64-w64-mingw32
VLC_INCLUDE ?= win/vlc-3.0-include

win64: libym_plugin.dll

win/libvlccore.dll.a: win/libvlccore.def
	$(MINGW)-dlltool -d $< -l $@ -D libvlccore.dll

libym_plugin.dll: src/ym.c src/version.h win/ym_plugin.rc $(STSOUND_SRC) win/libvlccore.dll.a
	$(MINGW)-gcc -O2 -DMODULE_STRING=\"ym\" -D__PLUGIN__ -D_WIN32_WINNT=0x0601 \
	       -IStSoundLibrary -I$(VLC_INCLUDE) -std=gnu11 -Wall -c src/ym.c -o ym.o
	$(MINGW)-g++ -O2 -IStSoundLibrary $(CXXWARN) -c $(STSOUND_SRC)
	$(MINGW)-windres win/ym_plugin.rc -O coff -o ym_res.o
	$(MINGW)-g++ -shared -o $@ ym.o ym_res.o $(STSOUND_OBJ) -static -static-libstdc++ -static-libgcc \
	       win/libvlccore.dll.a -lws2_32 -Wl,--no-undefined
	$(MINGW)-strip $@
	rm -f *.o

clean:
	rm -f *.o libym_plugin.so libym_plugin.dll

.PHONY: all install win64 clean
