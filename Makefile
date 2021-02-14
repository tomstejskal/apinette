PKG_CONFIG = pkg-config

CC = clang
COMMON_CFLAGS =
COMMON_LIBS = -lc

DEPS = lua libcurl jansson
DEPS_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(DEPS))
DEPS_LIBS = $(shell $(PKG_CONFIG) --libs $(DEPS))

ifeq ($(DEBUG),1)
  CFLAGS = -Wall -g3 -fsanitize=address -fno-omit-frame-pointer
  LIBS = -lasan
else
  CFLAGS = -Wall -O2
endif

CFLAGS += $(COMMON_CFLAGS) $(DEPS_CFLAGS)
LIBS += $(COMMON_LIBS) $(DEPS_LIBS)

apinette: main.o apinette.o base64.o linenoise.o
	$(CC) -o $@ $+ $(LIBS)

main.o: main.c utlist.h utstring.h linenoise/linenoise.h
	$(CC) $(CFLAGS) -c $<

apinette.o: apinette.c apinette.h utlist.h utstring.h
	$(CC) $(CFLAGS) -c $<

base64.o: base64.c base64.h
	$(CC) $(CFLAGS) -c $<

linenoise.o: linenoise/linenoise.c linenoise/linenoise.h
	$(CC) $(CFLAGS) -c $<

clean:
	-rm -f apinette *.o
