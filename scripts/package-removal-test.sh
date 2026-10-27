#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 prepare|check" >&2
	exit 2
fi

mode=$1
project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
state_file="$project_dir/build/package-removal-test.state"
settings_dir=$(finddir B_USER_SETTINGS_DIRECTORY)
marker_dir="$settings_dir/BeClan/FeedKit"
marker_file="$marker_dir/.package-removal-test"
marker_content="FeedKit package removal preservation test"

if [ "$(uname -s)" != Haiku ]; then
	echo "The package-removal test must run on Haiku" >&2
	exit 1
fi

architecture=$(getarch)
case "$architecture" in
	x86_64)
		package_name=feedkit
		;;
	x86)
		package_name=feedkit_x86
		;;
	*)
		echo "Unsupported FeedKit package architecture: $architecture" >&2
		exit 2
		;;
esac

lib_dir=$(finddir B_SYSTEM_LIB_DIRECTORY)
server_dir=$(finddir B_SYSTEM_SERVERS_DIRECTORY)
add_ons_dir=$(finddir B_SYSTEM_ADDONS_DIRECTORY)

is_installed()
{
	for package_file in "/boot/system/packages/$package_name"-*.hpkg; do
		if [ -f "$package_file" ]; then
			return 0
		fi
	done
	return 1
}

case "$mode" in
	prepare)
		if ! is_installed; then
			echo "FeedKit must be installed before preparing the removal test" >&2
			exit 1
		fi
		if [ -e "$marker_file" ] || [ -e "$state_file" ]; then
			echo "A package-removal test is already prepared" >&2
			exit 1
		fi
		mkdir -p "$marker_dir" "$(dirname "$state_file")"
		printf '%s\n' "$marker_content" > "$marker_file"
		printf '%s\n' "$marker_file" > "$state_file"
		echo "Prepared user-data marker: $marker_file"
		echo "Now uninstall FeedKit, then run: make package-removal-check"
		;;
	check)
		if is_installed; then
			echo "FeedKit is still installed; uninstall it before checking removal" >&2
			exit 1
		fi
		for packaged_path in \
			"$lib_dir/libfeedkit.so" \
			"$server_dir/feed_server" \
			"$add_ons_dir/feed_kit/Parsers/Atom" \
			"$add_ons_dir/feed_kit/Parsers/RSS"
		do
			if [ -e "$packaged_path" ]; then
				echo "FAIL: packaged file remains after uninstall: $packaged_path" >&2
				exit 1
			fi
		done
		if [ ! -f "$state_file" ]; then
			echo "The package-removal test was not prepared" >&2
			exit 1
		fi
		prepared_marker=$(sed -n '1p' "$state_file")
		if [ "$prepared_marker" != "$marker_file" ]; then
			echo "The recorded marker path is invalid" >&2
			exit 1
		fi
		if [ ! -f "$marker_file" ]; then
			echo "FAIL: uninstall removed the FeedKit user-data marker" >&2
			exit 1
		fi
		if [ "$(sed -n '1p' "$marker_file")" != "$marker_content" ]; then
			echo "FAIL: uninstall changed the FeedKit user-data marker" >&2
			exit 1
		fi
		rm -f -- "$marker_file" "$state_file"
		echo "FeedKit package-removal test passed: uninstall preserved user data."
		;;
	*)
		echo "Usage: $0 prepare|check" >&2
		exit 2
		;;
esac
