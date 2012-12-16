BINUTILS_VER=2.23.1
BINUTILS_TAR=binutils-$(BINUTILS_VER).tar.bz2
BINUTILS_URL="http://mirrors.kernel.org/gnu/binutils/$(BINUTILS_TAR)"

NEWLIB_VER=1.20.0
NEWLIB_TAR=newlib-$(NEWLIB_VER).tar.gz
NEWLIB_URL="ftp://sources.redhat.com/pub/newlib/$(NEWLIB_TAR)"

GCC_VER=4.7.2
GCC_TAR=gcc-$(GCC_VER).tar.bz2
GCC_URL="http://mirrors.kernel.org/gnu/gcc/gcc-$(GCC_VER)/$(GCC_TAR)"

BASEDIR=$(shell pwd)

all: gcc

src/$(BINUTILS_TAR):
	wget -P src $(BINUTILS_URL)

src/$(NEWLIB_TAR):
	wget -P src $(NEWLIB_URL)

src/$(GCC_TAR):
	wget -P src $(GCC_URL)

src/binutils-$(BINUTILS_VER): src/$(BINUTILS_TAR)
	tar -C src -xf $<

src/newlib-$(NEWLIB_VER): src/$(NEWLIB_TAR)
	tar -C src -xf $<

src/gcc-$(GCC_VER): src/$(GCC_TAR) src/newlib-$(NEWLIB_VER)
	tar -C src -xf $<
	ln -s $(BASEDIR)/src/newlib-$(NEWLIB_VER)/newlib $@
	ln -s $(BASEDIR)/src/newlib-$(NEWLIB_VER)/libgloss $@

binutils: src/binutils-$(BINUTILS_VER)
	mkdir -p build/binutils
	cd build/binutils; \
	$(BASEDIR)/$</configure --target=sh-elf --prefix=$(BASEDIR)/inst; \
	$(MAKE) -j3; \
	$(MAKE) install

gcc: src/gcc-$(GCC_VER) binutils
	mkdir -p build/gcc
	cd build/gcc; \
	$(BASEDIR)/$</configure --target=sh-elf --prefix=$(BASEDIR)/inst -enable-languages=c --without-pkgversion --with-newlib; \
	$(MAKE) -j3; \
	$(MAKE) install

clean:
	rm -rf build inst

distclean: clean
	rm -rf src
