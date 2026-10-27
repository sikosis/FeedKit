#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
client="$root_dir/build/Clients/FeedInfoPopper"
log="$root_dir/build/notification-smoke-test.log"
client_pid=

cleanup()
{
	if [ -n "$client_pid" ] && kill -0 "$client_pid" 2>/dev/null; then
		kill "$client_pid" 2>/dev/null || true
		wait "$client_pid" 2>/dev/null || true
	fi
}

fail()
{
	echo "Notification smoke test failed: $1" >&2
	if [ -s "$log" ]; then
		echo "Output ($log):" >&2
		sed -n '1,240p' "$log" >&2
	fi
	exit 1
}

trap cleanup EXIT HUP INT TERM

[ "$(uname -s)" = Haiku ] || fail "this runtime check must be run on Haiku"
[ -f "$client" ] || fail "missing notification client: $client"

LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

: > "$log"
echo "Checking native FeedKit notification construction without sending a notification..."
"$client" --smoke-test > "$log" 2>&1 &
client_pid=$!

attempt=0
while kill -0 "$client_pid" 2>/dev/null && [ "$attempt" -lt 10 ]; do
	attempt=$((attempt + 1))
	sleep 1
done
if kill -0 "$client_pid" 2>/dev/null; then
	fail "notification client did not close within 10 seconds"
fi
if ! wait "$client_pid"; then
	client_pid=
	fail "notification client returned a failure status"
fi
client_pid=

grep -F 'FeedKit notification smoke test passed' "$log" >/dev/null 2>&1 \
	|| fail "notification client did not report successful construction"

trap - EXIT HUP INT TERM

echo "Notification smoke test passed: native notification construction completed without sending a notification."
