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

`# make -C toolchain`

but be aware that this will take some time and requires
about 3-4 GiB disk space.

### Build Firmware Image

The firmware configuration and build process requires the following
tools and libraries:

 * gcc 4.7+

 * gperf, bison/flex

 * cmake 2.8.4+

to start the configuration, run:

`# autogen.sh`

The configuration is based on the Linux's KConfig utility CLI.
For each configurable option, a prompt will appear. To choose
the default/previous option simply hit `<Enter>`-key on your
keyboard.

### Install Firmware

if you want to "install" your own firmware, you can either
do this manually, or by executing:

`# autogen.sh install`

This will place a copy with the right filename [adds API rev]
into /lib/firmware/[the default path on most Distributions].

## Contact

If you have any questions, reports or patches, you should write
to <linux-wireless@vger.kernel.org>.
