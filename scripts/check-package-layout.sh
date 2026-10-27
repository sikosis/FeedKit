#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 PACKAGE_ROOT ARCHITECTURE" >&2
	exit 2
fi

package_root=$1
architecture=$2
project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
header_manifest="$project_dir/packaging/public-headers.txt"

require_file()
{
	if [ ! -f "$1" ]; then
		echo "Package layout is missing: $1" >&2
		exit 1
	fi
}

case "$architecture" in
	x86_64)
	library_path="lib/libfeedkit.so"
	parser_path="add-ons/feed_kit/Parsers"
	;;
	x86)
	library_path="lib/x86/libfeedkit.so"
	parser_path="add-ons/x86/feed_kit/Parsers"
	;;
	*)
		echo "Unsupported FeedKit package architecture: $architecture" >&2
		exit 2
		;;
esac

require_file "$package_root/$library_path"
require_file "$package_root/servers/feed_server"
require_file "$package_root/$parser_path/Atom"
require_file "$package_root/$parser_path/RSS"

while IFS= read -r header || [ -n "$header" ]; do
	case "$header" in
		""|'#'*) continue ;;
	esac
	require_file "$package_root/develop/headers/libfeedkit/$header"
done < "$header_manifest"

require_file "$package_root/documentation/packages/feedkit/README.md"
require_file "$package_root/documentation/packages/feedkit/DESIGN.md"
require_file "$package_root/documentation/packages/feedkit/LICENSE"

if [ -e "$package_root/develop/headers/libfeedkit/Private/Constants.h" ]; then
	echo "Private/Constants.h must not be exported by the development package" >&2
	exit 1
fi

expected_count=$(awk 'NF && $1 !~ /^#/ { count++ } END { print count + 0 }' \
	"$header_manifest")
actual_count=$(find "$package_root/develop/headers/libfeedkit" -type f -name '*.h' \
	| wc -l | tr -d ' ')
if [ "$actual_count" -ne "$expected_count" ]; then
	echo "Expected $expected_count public headers, found $actual_count" >&2
	exit 1
fi

expected_total=$((expected_count + 7))
actual_total=$(find "$package_root" -type f | wc -l | tr -d ' ')
if [ "$actual_total" -ne "$expected_total" ]; then
	echo "Expected $expected_total package files, found $actual_total" >&2
	exit 1
fi

if find "$package_root" -type l | grep . >/dev/null 2>&1; then
	echo "The staged package must not contain symlinks" >&2
	exit 1
fi

echo "FeedKit package layout passed: 4 runtime artifacts and $actual_count public headers."
