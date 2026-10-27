#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
	echo "Usage: $0 EXPECTED_ARCH VERSION REVISION" >&2
	exit 2
fi

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
expected_arch=${1:-}
version=$2
revision=$3

fail()
{
	echo "Release validation failed: $1" >&2
	exit 1
}

[ "$(uname -s)" = Haiku ] || fail "this check must run on Haiku"
command -v getarch >/dev/null 2>&1 || fail "getarch is unavailable"

architecture=$(getarch)
case "$architecture" in
	x86_64)
		package_name=feedkit
		package_architecture=x86_64
		;;
	x86)
		package_name=feedkit_x86
		package_architecture=x86_gcc2
		;;
	*)
		fail "unsupported active architecture: $architecture"
		;;
esac

if [ -n "$expected_arch" ] && [ "$architecture" != "$expected_arch" ]; then
	fail "expected $expected_arch but the active architecture is $architecture"
fi

validation_dir=$(mktemp -d \
	"$root_dir/build/release-$architecture-XXXXXX") \
	|| fail "could not create a release validation directory"
package_root="$validation_dir/package-root"
package_file="$validation_dir/$package_name-$version-$revision-$package_architecture.hpkg"

echo "Validating FeedKit for $architecture..."
make -C "$root_dir" clean
make -C "$root_dir" test
make -C "$root_dir" goal7-check
make -C "$root_dir" package \
	PACKAGE_ARCH="$architecture" \
	PACKAGE_ROOT="$package_root" \
	PACKAGE_FILE="$package_file"

[ -f "$package_file" ] || fail "package generation did not produce $package_file"

echo
echo "FeedKit release validation passed for $architecture."
echo "Validation package: $package_file"
echo "Install this package with pkgman, then run make installed-package-check."
echo "Afterwards run the documented package-removal prepare/uninstall/check sequence."
