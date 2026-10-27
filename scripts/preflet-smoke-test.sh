#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
preflet="$root_dir/build/FeedKitSettings"
log="$root_dir/build/preflet-smoke-test.log"
preflet_pid=
store=

fail()
{
	echo "Preferences smoke test failed: $1" >&2
	if [ -s "$log" ]; then
		echo "Output ($log):" >&2
		sed -n '1,240p' "$log" >&2
	fi
	exit 1
}

cleanup()
{
	if [ -n "$preflet_pid" ] && kill -0 "$preflet_pid" 2>/dev/null; then
		kill "$preflet_pid" 2>/dev/null || true
		wait "$preflet_pid" 2>/dev/null || true
	fi
	case "$store" in
		"$root_dir"/build/preflet-store-*) rm -rf -- "$store" ;;
	esac
}

trap cleanup EXIT HUP INT TERM

[ "$(uname -s)" = Haiku ] || fail "this runtime check must be run on Haiku"
[ -f "$preflet" ] || fail "missing preferences build artifact: $preflet"

store=$(mktemp -d "$root_dir/build/preflet-store-XXXXXX") \
	|| fail "could not create the isolated preferences store"
LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

: > "$log"
echo "Opening FeedKit preferences with an isolated settings directory..."
FEEDKIT_PREFLET_TEST_SETTINGS="$store" "$preflet" --smoke-test \
	> "$log" 2>&1 &
preflet_pid=$!

attempt=0
while kill -0 "$preflet_pid" 2>/dev/null && [ "$attempt" -lt 10 ]; do
	attempt=$((attempt + 1))
	sleep 1
done
if kill -0 "$preflet_pid" 2>/dev/null; then
	fail "preferences did not close within 10 seconds"
fi
if ! wait "$preflet_pid"; then
	preflet_pid=
	fail "preferences returned a failure status"
fi
preflet_pid=

grep -F 'FeedKit preferences smoke test passed' "$log" >/dev/null 2>&1 \
	|| fail "preferences did not report successful launch and shutdown"

cleanup
trap - EXIT HUP INT TERM

echo "Preferences smoke test passed: FeedKitSettings opened and closed cleanly with isolated settings."
