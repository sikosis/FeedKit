#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
deskbar_client="$root_dir/build/Clients/DeskbarIcon"
log="$root_dir/build/deskbar-smoke-test.log"
deskbar_pid=

cleanup()
{
	if [ -n "$deskbar_pid" ] && kill -0 "$deskbar_pid" 2>/dev/null; then
		kill "$deskbar_pid" 2>/dev/null || true
		wait "$deskbar_pid" 2>/dev/null || true
	fi
}

fail()
{
	echo "Deskbar smoke test failed: $1" >&2
	if [ -s "$log" ]; then
		echo "Output ($log):" >&2
		sed -n '1,240p' "$log" >&2
	fi
	exit 1
}

trap cleanup EXIT HUP INT TERM

[ "$(uname -s)" = Haiku ] || fail "this runtime check must be run on Haiku"
[ -f "$deskbar_client" ] || fail "missing Deskbar build artifact: $deskbar_client"

LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

: > "$log"
echo "Checking the FeedKit Deskbar view lifecycle without modifying Deskbar..."
"$deskbar_client" --smoke-test > "$log" 2>&1 &
deskbar_pid=$!

attempt=0
while kill -0 "$deskbar_pid" 2>/dev/null && [ "$attempt" -lt 10 ]; do
	attempt=$((attempt + 1))
	sleep 1
done
if kill -0 "$deskbar_pid" 2>/dev/null; then
	fail "Deskbar lifecycle check did not finish within 10 seconds"
fi
if ! wait "$deskbar_pid"; then
	deskbar_pid=
	fail "Deskbar client returned a failure status"
fi
deskbar_pid=

grep -F 'FeedKit Deskbar smoke test passed' "$log" >/dev/null 2>&1 \
	|| fail "Deskbar client did not report a successful lifecycle check"

trap - EXIT HUP INT TERM

echo "Deskbar smoke test passed: the view constructed, archived, restored, and closed without modifying Deskbar."
