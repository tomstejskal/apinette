PKG_CONFIG = pkg-config

CC = clang

DEPS = lua libcurl jansson
DEPS_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(DEPS))
DEPS_LDFLAGS = $(shell $(PKG_CONFIG) --libs $(DEPS))

ifdef DEBUG
  CFLAGS = -Wall -g3 -fsanitize=address -fno-omit-frame-pointer
  LDFLAGS = -lasan
else
  CFLAGS = -Wall -O2
  LDFLAGS =
endif

CFLAGS += $(DEPS_CFLAGS)
LDFLAGS += $(DEPS_LDFLAGS)

apinette: main.o apinette.o base64.o linenoise/linenoise.o

main.o: utlist.h utstring.h linenoise/linenoise.h
apinette.o: apinette.h utlist.h utstring.h
base64.o: base64.h
linenoise/linenoise.o: linenoise/linenoise.h

.PHONY: clean
clean:
	-rm -f apinette *.o linenoise/*.o
