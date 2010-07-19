# ===========================================================================
# carl9170 configuration targets
# These targets are used from top-level makefile

PHONY += config

config: conf

# ===========================================================================
# Shared Makefile for the various kconfig executables:

clean:
	@rm -f *.o zconf.tab.c lex.zconf.c zconf.hash.c lex.backup conf

# generated files seem to need this to find local include files

zconf.tab.o: lex.zconf.c zconf.hash.c

zconf.tab.c: zconf.y
lex.zconf.c: zconf.l
zconf.hash.c: zconf.gperf

%.tab.c: %.y
	bison -l -b $* -p $(notdir $*) $<

lex.%.c: %.l
	flex -L -P$(notdir $*) -o$@ $<

%.hash.c: %.gperf
	gperf < $< > $@

%.o: %.c
	gcc $(CFLAGS) -I./ -c $< -o $@

conf: zconf.tab.o conf.o
