#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 BUILD_DIRECTORY PACKAGE_ROOT" >&2
	exit 2
fi

build_dir=$1
package_root=$2
project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
header_manifest="$project_dir/packaging/public-headers.txt"

case "$package_root" in
	""|/)
		echo "Refusing unsafe package root: '$package_root'" >&2
		exit 2
		;;
esac

for artifact in \
	"$build_dir/libfeedkit.so" \
	"$build_dir/feed_server" \
	"$build_dir/Parsers/Atom" \
	"$build_dir/Parsers/RSS"
do
	if [ ! -f "$artifact" ]; then
		echo "Missing package artifact: $artifact" >&2
		exit 1
	fi
done

copy_file()
{
	source_file=$1
	destination=$2

	if command -v copyattr >/dev/null 2>&1; then
		copyattr --data "$source_file" "$destination"
	else
		cp -p "$source_file" "$destination"
	fi
}

lib_dir="$package_root/lib"
server_dir="$package_root/servers"
parser_dir="$package_root/add-ons/feed_kit/Parsers"
header_dir="$package_root/develop/headers/libfeedkit"
doc_dir="$package_root/documentation/packages/feedkit"

mkdir -p "$lib_dir" "$server_dir" "$parser_dir" "$header_dir" "$doc_dir"

copy_file "$build_dir/libfeedkit.so" "$lib_dir/libfeedkit.so"
copy_file "$build_dir/feed_server" "$server_dir/feed_server"
copy_file "$build_dir/Parsers/Atom" "$parser_dir/Atom"
copy_file "$build_dir/Parsers/RSS" "$parser_dir/RSS"

while IFS= read -r header || [ -n "$header" ]; do
	case "$header" in
		""|'#'*) continue ;;
	esac

	source_header="$project_dir/libfeedkit/$header"
	destination_header="$header_dir/$header"
	if [ ! -f "$source_header" ]; then
		echo "Public header is missing: $source_header" >&2
		exit 1
	fi

	mkdir -p "$(dirname "$destination_header")"
	copy_file "$source_header" "$destination_header"
done < "$header_manifest"

for document in README.md DESIGN.md LICENSE; do
	copy_file "$project_dir/$document" "$doc_dir/$document"
done

echo "Staged FeedKit package tree at $package_root"
