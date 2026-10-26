#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
test_binary="$project_dir/build/Tests/feedkit_lifecycle_tests"

if [ "$(uname -s)" != Haiku ]; then
	echo "The installed-package test must run on Haiku" >&2
	exit 1
fi

if [ ! -x "$test_binary" ]; then
	echo "Missing installed-package test executable: $test_binary" >&2
	exit 1
fi

installed=false
for package_file in /boot/system/packages/feedkit-*.hpkg; do
	if [ -f "$package_file" ]; then
		installed=true
		break
	fi
done
if [ "$installed" != true ]; then
	echo "FeedKit is not installed in /boot/system/packages" >&2
	exit 1
fi

for shadow_library in \
	/boot/system/non-packaged/lib/libfeedkit.so \
	/boot/home/config/non-packaged/lib/libfeedkit.so
do
	if [ -e "$shadow_library" ]; then
		echo "A non-packaged library would shadow the HPKG: $shadow_library" >&2
		exit 1
	fi
done

unset LIBRARY_PATH
exec "$test_binary"
