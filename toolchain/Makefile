BINUTILS_VER=2.27
BINUTILS_TAR=binutils-$(BINUTILS_VER).tar.bz2
BINUTILS_URL="http://mirrors.kernel.org/gnu/binutils/$(BINUTILS_TAR)"

NEWLIB_VER=2.4.0
NEWLIB_TAR=newlib-$(NEWLIB_VER).tar.gz
NEWLIB_URL="ftp://sourceware.org/pub/newlib/$(NEWLIB_TAR)"

GCC_VER=6.2.0
GCC_TAR=gcc-$(GCC_VER).tar.bz2
GCC_URL="http://mirrors.kernel.org/gnu/gcc/gcc-$(GCC_VER)/$(GCC_TAR)"

JOBS?=$(shell grep -c ^processor /proc/cpuinfo)

BASEDIR=$(shell pwd)

define checksum
@if grep -q '  $(subst .,\.,$(1))$$' SHA256SUMS; then			\
	grep '  $(subst .,\.,$(1))$$' SHA256SUMS | sha256sum -c || rm $(1);	\
else									\
	echo "WARNING: no checksum defined for $(1)";			\
fi
endef

all: clean gcc

src/$(BINUTILS_TAR):
	wget -P "$(BASEDIR)/src" $(BINUTILS_URL)
	$(call checksum,$@)

src/$(NEWLIB_TAR):
	wget -P "$(BASEDIR)/src" $(NEWLIB_URL)
	$(call checksum,$@)

src/$(GCC_TAR):
	wget -P "$(BASEDIR)/src" $(GCC_URL)
	$(call checksum,$@)

src/binutils-$(BINUTILS_VER): src/$(BINUTILS_TAR)
	tar -C "$(BASEDIR)/src" -xf $<

src/newlib-$(NEWLIB_VER): src/$(NEWLIB_TAR)
	tar -C "$(BASEDIR)/src" -xf $<

src/gcc-$(GCC_VER): src/$(GCC_TAR) src/newlib-$(NEWLIB_VER)
	tar -C "$(BASEDIR)/src" -xf $<
	ln -s $(BASEDIR)/src/newlib-$(NEWLIB_VER)/newlib $@
	ln -s $(BASEDIR)/src/newlib-$(NEWLIB_VER)/libgloss $@

binutils: src/binutils-$(BINUTILS_VER)
	mkdir -p "$(BASEDIR)/build/binutils"
	cd "$(BASEDIR)/build/binutils"; \
	$(BASEDIR)/$</configure --target=sh-elf --prefix=$(BASEDIR)/inst; \
	$(MAKE) -j$(JOBS); \
	$(MAKE) install

gcc: src/gcc-$(GCC_VER) binutils
	mkdir -p "$(BASEDIR)/build/gcc"
	cd "$(BASEDIR)/build/gcc"; \
	$(BASEDIR)/$</configure --target=sh-elf --prefix=$(BASEDIR)/inst -enable-languages=c --without-pkgversion --with-newlib; \
	$(MAKE) -j$(JOBS); \
	$(MAKE) install

clean:
	rm -rf "$(BASEDIR)/build" "$(BASEDIR)/inst"

distclean: clean
	rm -rf "$(BASEDIR)/src"
