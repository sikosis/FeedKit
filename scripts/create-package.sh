#!/bin/sh

set -eu

if [ "$#" -ne 5 ]; then
	echo "Usage: $0 PACKAGE_ROOT OUTPUT VERSION REVISION ARCHITECTURE" >&2
	exit 2
fi

package_root=$1
output=$2
version=$3
revision=$4
architecture=$5
project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
template="$project_dir/packaging/PackageInfo.in"
package_info="$package_root/.PackageInfo"

if [ "$(uname -s)" != Haiku ]; then
	echo "HPKG creation must run on Haiku" >&2
	exit 1
fi

if ! command -v package >/dev/null 2>&1; then
	echo "The Haiku 'package' command is unavailable" >&2
	exit 1
fi

case "$version" in
	""|*[!0-9A-Za-z._~+]*)
		echo "Invalid package version: $version" >&2
		exit 2
		;;
esac

case "$revision" in
	""|*[!0-9]*)
		echo "Invalid package revision: $revision" >&2
		exit 2
		;;
esac

case "$architecture" in
	x86_64)
		package_name=feedkit
		package_architecture=x86_64
		secondary_suffix=
		library_path=lib/libfeedkit.so
		parser_path=add-ons/feed_kit/Parsers
		;;
	x86)
		package_name=feedkit_x86
		package_architecture=x86_gcc2
		secondary_suffix=_x86
		library_path=lib/x86/libfeedkit.so
		parser_path=add-ons/x86/feed_kit/Parsers
		;;
	*)
		echo "Unsupported FeedKit package architecture: $architecture" >&2
		exit 2
		;;
esac

if [ -e "$package_info" ]; then
	echo "Refusing to replace existing package metadata: $package_info" >&2
	exit 1
fi

package_root=$(CDPATH= cd -- "$package_root" && pwd)
package_info="$package_root/.PackageInfo"
output_dir=$(dirname "$output")
mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
output="$output_dir/$(basename "$output")"

case "$output" in
	"$package_root"/*)
		echo "Package output must be outside the staged package root" >&2
		exit 2
		;;
esac

if [ -e "$output" ]; then
	echo "Refusing to replace existing package: $output" >&2
	echo "Remove an unneeded local artifact or increase PACKAGE_REVISION." >&2
	exit 1
fi

cleanup()
{
	rm -f -- "$package_info"
}
trap cleanup EXIT HUP INT TERM

sed \
	-e "s/@PACKAGE_NAME@/$package_name/g" \
	-e "s/@SECONDARY_SUFFIX@/$secondary_suffix/g" \
	-e "s/@VERSION@/$version/g" \
	-e "s/@REVISION@/$revision/g" \
	-e "s/@ARCHITECTURE@/$package_architecture/g" \
	"$template" > "$package_info"

(cd "$package_root" && package create "$output")

info_file="$output.info"
paths_file="$output.paths"
package list -i "$output" > "$info_file"
package list -p "$output" > "$paths_file"

grep -F "name: $package_name" "$info_file" >/dev/null
grep -F "version: $version-$revision" "$info_file" >/dev/null
grep -F "architecture: $package_architecture" "$info_file" >/dev/null
grep -F "servers/feed_server" "$paths_file" >/dev/null
grep -F "$parser_path/Atom" "$paths_file" >/dev/null
grep -F "$parser_path/RSS" "$paths_file" >/dev/null
grep -F "$library_path" "$paths_file" >/dev/null

rm -f -- "$info_file" "$paths_file"
trap - EXIT HUP INT TERM
cleanup

echo "Created and inspected FeedKit package: $output"
