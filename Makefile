CFLAGS = -g

ifeq ($(OS),Windows_NT)
	CFLAGS += -lws2_32 
	EXE_SUFFIX = .exe
endif

all: TAKtick

TAKtick: TAKtick.c Makefile
	gcc TAKtick.c $(CFLAGS) -o $@
	strip TAKtick$(EXE_SUFFIX)

clean:
	rm -f TAKtick$(EXE_SUFFIX)

