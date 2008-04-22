#
# Wmake File - for Watcom's wmake
# Use 'wmake -f Makefile.wat'

.BEFORE
	@set INCLUDE=.;$(%watcom)\H;$(%watcom)\H\NT
	@set LIB=.;$(%watcom)\LIB386

cc     = wcc386
cflags = -zq
lflags = OPT quiet OPT map LIBRARY ..\libmseed\libmseed.lib
cvars  = $+$(cvars)$- -DWIN32

BIN = ..\mseed2ascii.exe

INCS = -I..\libmseed

all: $(BIN)

$(BIN):	mseed2ascii.obj
	wlink $(lflags) name $(BIN) file {mseed2ascii.obj}

# Source dependencies:
mseed2ascii.obj:	mseed2ascii.c

# How to compile sources:
.c.obj:
	$(cc) $(cflags) $(cvars) $(INCS) $[@ -fo=$@

# Clean-up directives:
clean:	.SYMBOLIC
	del *.obj *.map $(BIN)
