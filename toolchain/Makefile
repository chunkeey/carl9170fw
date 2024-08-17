BINUTILS_VER=2.43.1
BINUTILS_TAR=binutils-$(BINUTILS_VER).tar.xz
BINUTILS_URL="http://mirrors.kernel.org/gnu/binutils/$(BINUTILS_TAR)"

NEWLIB_VER=4.4.0.20231231
NEWLIB_TAR=newlib-$(NEWLIB_VER).tar.gz
NEWLIB_URL="ftp://sourceware.org/pub/newlib/$(NEWLIB_TAR)"

GCC_VER=13.3.0
GCC_TAR=gcc-$(GCC_VER).tar.xz
GCC_URL="http://mirrors.kernel.org/gnu/gcc/gcc-$(GCC_VER)/$(GCC_TAR)"

MPFR_VER=4.2.1
MPFR_TAR=mpfr-$(MPFR_VER).tar.xz
MPFR_URL="http://ftpmirror.gnu.org/gnu/mpfr/$(MPFR_TAR)"

GMP_VER=6.3.0
GMP_TAR=gmp-$(GMP_VER).tar.xz
GMP_URL="http://ftpmirror.gnu.org/gnu/gmp/$(GMP_TAR)"

MPC_VER=1.3.1
MPC_TAR=mpc-$(MPC_VER).tar.gz
MPC_URL="http://ftpmirror.gnu.org/gnu/mpc/$(MPC_TAR)"

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

src/$(MPFR_TAR):
	wget -P "$(BASEDIR)/src" "$(MPFR_URL)"
	$(call checksum,$@)

src/$(GMP_TAR):
	wget -P "$(BASEDIR)/src" "$(GMP_URL)"
	$(call checksum,$@)

src/$(MPC_TAR):
	wget -P "$(BASEDIR)/src" "$(MPC_URL)"
	$(call checksum,$@)

src/$(BINUTILS_TAR):
	wget -P "$(BASEDIR)/src" "$(BINUTILS_URL)"
	$(call checksum,$@)

src/$(NEWLIB_TAR):
	wget -P "$(BASEDIR)/src" "$(NEWLIB_URL)"
	$(call checksum,$@)

src/$(GCC_TAR):
	wget -P "$(BASEDIR)/src" "$(GCC_URL)"
	$(call checksum,$@)

src/mpfr-$(MPFR_VER): src/$(MPFR_TAR)
	tar -C "$(BASEDIR)/src" -xf "$<"

src/gmp-$(GMP_VER): src/$(GMP_TAR)
	tar -C "$(BASEDIR)/src" -xf "$<"

src/mpc-$(MPC_VER): src/$(MPC_TAR)
	tar -C "$(BASEDIR)/src" -xf "$<"

src/binutils-$(BINUTILS_VER): src/$(BINUTILS_TAR)
	tar -C "$(BASEDIR)/src" -xf "$<"

src/newlib-$(NEWLIB_VER): src/$(NEWLIB_TAR)
	tar -C "$(BASEDIR)/src" -xf "$<"

src/gcc-$(GCC_VER): src/$(GCC_TAR) src/newlib-$(NEWLIB_VER) src/mpc-$(MPC_VER) src/gmp-$(GMP_VER) src/mpfr-$(MPFR_VER)
	tar -C "$(BASEDIR)/src" -xf "$<"
	ln -s "$(BASEDIR)/src/newlib-$(NEWLIB_VER)/newlib" "$@"
	ln -s "$(BASEDIR)/src/newlib-$(NEWLIB_VER)/libgloss" "$@"
	ln -s "$(BASEDIR)/src/gmp-$(GMP_VER)" "$@/gmp"
	ln -s "$(BASEDIR)/src/mpfr-$(MPFR_VER)" "$@/mpfr"
	ln -s "$(BASEDIR)/src/mpc-$(MPC_VER)" "$@/mpc"

binutils: src/binutils-$(BINUTILS_VER)
	mkdir -p "$(BASEDIR)/build/binutils"
	cd "$(BASEDIR)/build/binutils"; \
	"$(BASEDIR)"/$</configure --target=sh-elf --prefix="$(BASEDIR)/inst"; \
	$(MAKE) -j$(JOBS); \
	$(MAKE) install

gcc: src/gcc-$(GCC_VER) binutils
	mkdir -p "$(BASEDIR)/build/gcc"
	cd "$(BASEDIR)/build/gcc"; \
	"$(BASEDIR)"/$</configure --target=sh-elf --prefix="$(BASEDIR)/inst" -enable-languages=c --without-pkgversion --with-newlib; \
	$(MAKE) -j$(JOBS); \
	$(MAKE) install

clean:
	rm -rf "$(BASEDIR)/build" "$(BASEDIR)/inst"

distclean: clean
	rm -rf "$(BASEDIR)/src"
