cmake_minimum_required(VERSION 3.5)

project(carl9170.fw)

include("../extra/sh-elf-linux.cmake")
include("../config.cmake")

set(CARL9170_FW_ELF carl9170.elf)
set(CARLFW_CFLAGS_WARNING "-W -Wall -Wextra -Wunreachable-code -Winline -Wlogical-op -Wno-packed-bitfield-compat -Winit-self -Wshadow -Wwrite-strings -Waggregate-return -Wstrict-prototypes -Wformat=2 -Wcast-align -Wmissing-format-attribute -Wmissing-prototypes -Wtype-limits -Wmissing-declarations -Wmissing-noreturn -Wredundant-decls -Wnested-externs -Wdisabled-optimization -Wpointer-arith -Wvolatile-register-var -Waddress -Wbad-function-cast -Wunsafe-loop-optimizations")
set(CARLFW_CFLAGS_EXTRA "-mbitops -ffunction-sections -Wframe-larger-than=128 -Werror")
set(CARLFW_CFLAGS_DEF "-D__CARL9170FW__")
if (CONFIG_CARL9170FW_AGGRESSIVE_CFLAGS)
	set(CARLFW_CFLAGS_AGGRESSIVE "-fomit-frame-pointer -fsee -frename-registers -ftree-vectorize -flto -fstrict-volatile-bitfields -fmodulo-sched -fwhole-program")
endif (CONFIG_CARL9170FW_AGGRESSIVE_CFLAGS)
set(CARLFW_CFLAGS "${CARLFW_CFLAGS_DEF} ${CARLFW_CFLAGS_EXTRA} ${CARLFW_CFLAGS_AGGRESSIVE} ${CARLFW_CFLAGS_WARNING}")

include_directories (../include/linux ../include/shared ../include include)

set(carl9170_main_src src/main.c src/wlan.c src/wlanrx.c src/wlantx.c
		      src/fw.c src/gpio.c src/timer.c
		      src/uart.c src/dma.c src/hostif.c src/reboot.S
		      src/printf.c src/rf.c src/cam.c src/wol.c)

set(carl9170_lib_src src/memcpy.S src/memset.S src/udivsi3_i4i-Os.S)
set(carl9170_usb_src usb/main.c usb/usb.c usb/fifo.c)

set(carl9170_src ${carl9170_main_src} ${carl9170_lib_src} ${carl9170_usb_src})

#set_source_files_properties(src/ashlsi3.S PROPERTIES LANGUAGE C)
set_source_files_properties(src/memcpy.S PROPERTIES LANGUAGE C)
set_source_files_properties(src/memset.S PROPERTIES LANGUAGE C)
set_source_files_properties(src/reboot.S PROPERTIES LANGUAGE C)
set_source_files_properties(src/udivsi3_i4i-Os.S PROPERTIES LANGUAGE C)

add_executable(carl9170.elf ${carl9170_src})

set_target_properties(carl9170.elf PROPERTIES LINKER_LANGUAGE C)

set_target_properties(carl9170.elf PROPERTIES COMPILE_FLAGS "${CARLFW_CFLAGS}")
set_target_properties(carl9170.elf PROPERTIES LINK_FLAGS "${CARLFW_CFLAGS} -Wl,-Tcarl9170.lds")

add_custom_target(firmware ALL DEPENDS carl9170.elf)

add_custom_command(
	POST_BUILD
	DEPENDS carl9170.elf
	COMMAND ${OBJCOPY}
	ARGS --strip-unneeded -O binary -R .sram -R .eeprom -R .fwdsc carl9170.elf carl9170.bin
	TARGET firmware
	BYPRODUCTS carl9170.bin
	OUTPUTS carl9170.bin)

add_custom_command(
	POST_BUILD
	DEPENDS carl9170.elf
	COMMAND ${OBJCOPY}
	ARGS --strip-unneeded -O binary -j .fwdsc carl9170.elf carl9170.dsc
	TARGET firmware
	BYPRODUCTS carl9170.dsc
	OUTPUTS carl9170.dsc)

add_custom_command(
	POST_BUILD
	DEPENDS firmware
	TARGET firmware
	COMMAND cat
	ARGS "carl9170.bin" "carl9170.dsc" > "carl9170.fw"
	DEPENDS carl9170.elf carl9170.bin carl9170.dsc
	OUTPUTS carl9170.fw)

SET_DIRECTORY_PROPERTIES(
	PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "carl9170.fw")
