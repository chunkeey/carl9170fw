#!/bin/bash

set -e

case "$1" in
	config)
		echo "Configuring..."
		pushd config
		cmake .
		make
		popd
		shift 1
		config/conf Kconfig "$@"
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
		make

		echo -n "Installing firmware..."
		if [ "$CONFIG_CARL9170FW_BUILD_TOOLS" = "y" ]; then
			echo -n "Prepare firmware image..."
			tools/src/fwprepare carlfw/carl9170.fw

			if [ "$CONFIG_CARL9170FW_BUILD_MINIBOOT" = "y" ]; then
				echo -n "Apply miniboot..."
				# also update checksum
				tools/src/miniboot a carlfw/carl9170.fw minifw/miniboot.fw
			fi
		fi
		install -m 644 carlfw/carl9170.fw \
			../carl9170-$CONFIG_CARL9170FW_RELEASE_VERSION.fw
		echo "done."
	;;

	*)
		$0 config "$@"
		$0 compile
	;;


esac
