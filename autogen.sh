#!/bin/sh

set -e

case "$1" in
	config)
		echo "Configuring..."
		make -C config
		config/conf Kconfig
		cmake .
	;;

	compile)
		echo "Compile time..."
		make

	;;

	install)
		if [ ! -e .config ]; then
			exit 1
		fi

		. ./.config


		if [ "$CONFIG_CARL9170FW_MAKE_RELEASE" = "y" ]; then
			echo "Installing firmware..."
			tmpfwfile=`mktemp`
			cat carlfw/carl9170.fw carlfw/carl9170.dsc > $tmpfwfile
			install $tmpfwfile /lib/firmware/carl9170-$CONFIG_CARL9170FW_RELEASE_VERSION.fw
			rm $tmpfwfile
		fi
	;;

	*)
		$0 config
		$0 compile
	;;


esac
