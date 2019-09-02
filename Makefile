PREFIX   ?= /usr/local
LIBDIR   ?= lib
VSTDIR   ?= $(PREFIX)/$(LIBDIR)/lxvst
CXXFLAGS ?= -Wall
LDFLAGS  ?=
STRIP    ?= strip

###############################################################################

VSTNAME=lv2vst

###############################################################################

ifeq ($(DEBUG),)
  override CXXFLAGS += -msse -msse2 -ffast-math -fomit-frame-pointer -O3 -fno-finite-math-only -DNDEBUG
else
  override CXXFLAGS += -g -O0
endif

###############################################################################

UNAME=$(shell uname)
ifeq ($(UNAME),Darwin)
  STRIPFLAGS=-x
  LIB_EXT=.dylib
  VSTLDFLAGS=-dynamiclib
  LOADLIBES=-lm -ldl
  override CXXFLAGS += -Wno-deprecated-declarations
  override CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden -fdata-sections -ffunction-sections -fPIC -pthread
  override LDFLAGS  += -headerpad_max_install_names -Bsymbolic
  ifeq ($(DEBUG),)
    override LDFLAGS += -fvisibility=hidden -fdata-sections -ffunction-sections -Wl,-dead_strip
  endif
else
  STRIPFLAGS=-s
  VSTLDFLAGS=-Wl,-Bstatic -Wl,-Bdynamic -Wl,--as-needed -shared
  override CXXFLAGS  += -fvisibility=hidden -fdata-sections -ffunction-sections -mfpmath=sse
  ifeq ($(DEBUG),)
    override LDFLAGS += -Wl,--gc-sections -Wl,-O1 -Wl,--as-needed -Wl,--strip-all
  endif
  ifneq ($(XWIN),)
    CC=$(XWIN)-gcc
    CXX=$(XWIN)-g++
    STRIP=$(XWIN)-strip
    LIB_EXT=.dll
    LOADLIBES=-lm -lws2_32
    PTHREAD_DEP=pthread.o
    PTHREAD_FLAGS=-D__CLEANUP_C -DPTW32_BUILD_INLINED -DPTW32_STATIC_LIB -DHAVE_PTW32_CONFIG_H -Ilib/pthreads-w32/
    override CXXFLAGS += -mstackrealign $(PTHREAD_FLAGS)
    override LDFLAGS += -static-libgcc -static-libstdc++
  else
    LIB_EXT=.so
    LOADLIBES=-lm -ldl
    override CXXFLAGS += -fPIC -pthread
    override LDFLAGS += -static-libgcc -static-libstdc++
  endif
endif

###############################################################################

PLUGIN_SRC= \
  src/instantiate.cc \
  src/loadlib.cc \
  src/lv2ttl.cc \
  src/lv2vst.cc \
  src/lv2vstui.cc \
  src/state.cc \
  src/vstmain.cc \
  src/worker.cc

PLUGIN_DEP= \
  src/loadlib.h \
  src/lv2desc.h \
  src/lv2vst.h \
  src/lv2ttl.h \
  src/ringbuffer.h \
  src/shell.h \
  src/uri_map.h \
  src/vst.h \
  src/worker.h

LV2SRC= \
  lib/lilv/collections.c \
  lib/lilv/instance.c \
  lib/lilv/lib.c \
  lib/lilv/node.c \
  lib/lilv/plugin.c \
  lib/lilv/pluginclass.c \
  lib/lilv/port.c \
  lib/lilv/query.c \
  lib/lilv/scalepoint.c \
  lib/lilv/state.c \
  lib/lilv/ui.c \
  lib/lilv/util.c \
  lib/lilv/world.c \
  lib/lilv/zix/tree.c \
  lib/serd/env.c \
  lib/serd/node.c \
  lib/serd/reader.c \
  lib/serd/string.c \
  lib/serd/uri.c \
  lib/serd/writer.c \
  lib/sord/sord.c \
  lib/sord/syntax.c \
  lib/sord/zix/btree.c \
  lib/sord/zix/digest.c \
  lib/sord/zix/hash.c \
  lib/sratom/sratom.c

INCLUDES= \
  include/vestige.h \
  include/lilv_config.h \
  include/lilv/lilv.h \
  include/serd_config.h \
  include/serd/serd.h \
  include/sord_config.h \
  include/sord/sord.h \
  include/sratom/sratom.h \
  include/lv2/lv2plug.in

###############################################################################

ifneq ($(BUNDLES),)
  override CXXFLAGS+=-DBUNDLES="\"$(BUNDLES)\""
endif
ifneq ($(WHITELIST),)
  override CXXFLAGS+=-DWHITELIST="\"$(WHITELIST)\""
endif

###############################################################################
all: $(VSTNAME)$(LIB_EXT)

$(VSTNAME)$(LIB_EXT): $(PLUGIN_SRC) $(PLUGIN_DEP) $(LV2SRC) $(INCLUDES) $(PTHREAD_DEP) Makefile
	$(CXX) $(CPPFLAGS) -I. -Isrc $(CXXFLAGS) \
		-Iinclude/ -Ilib/sord/ -Ilib/lilv/ \
		-o $(VSTNAME)$(LIB_EXT) \
		$(PLUGIN_SRC) \
		$(LV2SRC) \
		$(VSTLDFLAGS) $(LDFLAGS) $(PTHREAD_DEP) $(LOADLIBES)
ifeq ($(DEBUG),)
	$(STRIP) $(STRIPFLAGS) $(VSTNAME)$(LIB_EXT)
endif

pthread.o: lib/pthreads-w32/pthread.c Makefile
	$(CC) $(PTHREAD_FLAGS) \
		-fvisibility=hidden -mstackrealign -Wall -O3 \
		-c -o pthread.o \
		lib/pthreads-w32/pthread.c

$(VSTNAME).x86_64.dylib: $(PLUGIN_SRC) $(PLUGIN_DEP) $(LV2SRC) $(INCLUDES) $(PTHREAD_DEP) Makefile
	$(MAKE) CXXFLAGS="-arch x86_64" $(VSTNAME).dylib
	mv $(VSTNAME).dylib $(VSTNAME).x86_64.dylib

$(VSTNAME).i386.dylib: $(PLUGIN_SRC) $(PLUGIN_DEP) $(LV2SRC) $(INCLUDES) $(PTHREAD_DEP) Makefile
	$(MAKE) CXXFLAGS="-arch i386" $(VSTNAME).dylib
	mv $(VSTNAME).dylib $(VSTNAME).i386.dylib

osxbundle: lv2vst.x86_64.dylib lv2vst.i386.dylib
	mkdir -p lv2.vst/Contents/MacOS
	echo "BNDL????" > lv2.vst/Contents/PkgInfo
	sed "s/@EXEC@/$(VSTNAME)/;s/@ICON@//;s/@VERSION@/0.1.0/" \
		misc/plist > lv2.vst/Contents/Info.plist
	lipo -create -o lv2.vst/Contents/MacOS/$(VSTNAME) \
		$(VSTNAME).x86_64.dylib $(VSTNAME).i386.dylib

clean:
	rm -f $(VSTNAME)*$(LIB_EXT) pthread.o
	rm -rf lv2.vst

install: all
	install -d $(DESTDIR)$(VSTDIR)
	install -m755 $(VSTNAME)$(LIB_EXT) $(DESTDIR)$(VSTDIR)/

uninstall:
	rm -f $(DESTDIR)$(VSTDIR)/$(VSTNAME)$(LIB_EXT)
	-rmdir $(DESTDIR)$(VSTDIR)

.PHONY:clean install uninstall lv2ttl.h osxbundle
