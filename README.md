# Community AR9170 Linux firmware

## Introduction

This is the firmware for the Atheros ar9170 802.11n devices.
The firmware, carl9170.fw, can be used with the carl9170 Linux
driver or the tools supplied in the repository.

## Build

### Build Toolchain

To build the firmware you will need an SH-2 toolchain.
You can use the makefile in this repository to build
your own toolchain:

`$ make -C toolchain`

but be aware that this will take some time and requires
about 3-5 GiB disk space.

### Build Firmware Image

The firmware configuration and build process requires the following
tools and libraries:

 * gcc 6.0+ (including library and header dependencies)

 * bison/flex

 * cmake 3.8+

to start the configuration, run:

`$ autogen.sh`

The configuration is based on the Linux's KConfig utility CLI.
For each configurable option, a prompt will appear. To choose
the default/previous option simply hit `<Enter>`-key on your
keyboard.

If you encounter the error:

`../toolchain/inst/bin/sh-elf-objcopy: 'carl9170.elf': No such file`

, run

`$ make carl9170.elf`

### Install Firmware

if you want to "install" your own firmware, you can either
do this manually, or by executing:

`# autogen.sh install`

This will place a copy with the right filename [adds API rev]
into /lib/firmware/[the default path on most Distributions].

## Contact

If you have any patches, you should write
to <linux-wireless@vger.kernel.org> and
include "carl9170" in the subject line.
