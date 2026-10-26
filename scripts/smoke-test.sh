#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
server="$root_dir/build/feed_server"
atom="$root_dir/build/Parsers/Atom"
rss="$root_dir/build/Parsers/RSS"
log="$root_dir/build/smoke-test.log"
server_pid=

fail()
{
	echo "Smoke test failed: $1" >&2
	if [ -f "$log" ]; then
		echo "Server output ($log):" >&2
		sed -n '1,240p' "$log" >&2
	fi
	exit 1
}

cleanup()
{
	if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
		kill "$server_pid" 2>/dev/null || true
		wait "$server_pid" 2>/dev/null || true
	fi
}

trap cleanup EXIT HUP INT TERM

if [ "$(uname -s)" != "Haiku" ]; then
	fail "this runtime check must be run on Haiku"
fi

for artifact in "$server" "$atom" "$rss"; do
	[ -f "$artifact" ] || fail "missing build artifact: $artifact"
done

LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

: > "$log"
echo "Starting feed_server smoke mode (log: $log)..."
"$server" --smoke-test > "$log" 2>&1 &
server_pid=$!
echo "Waiting up to 15 seconds for startup, parser loading, and clean shutdown..."

attempt=0
while kill -0 "$server_pid" 2>/dev/null && [ "$attempt" -lt 15 ]; do
	attempt=$((attempt + 1))
	sleep 1
done

if kill -0 "$server_pid" 2>/dev/null; then
	fail "feed_server did not complete smoke mode within 15 seconds"
fi

if ! wait "$server_pid"; then
	server_pid=
	fail "feed_server returned a failure status"
fi

server_pid=

grep -F 'Loading "Atom"... Done' "$log" >/dev/null 2>&1 \
	|| fail "Atom parser did not load"
grep -F 'Loading "RSS"... Done' "$log" >/dev/null 2>&1 \
	|| fail "RSS parser did not load"
grep -F 'FeedServer smoke test: ReadyToRun complete' "$log" >/dev/null 2>&1 \
	|| fail "feed_server did not reach ReadyToRun"
grep -F 'FeedServer smoke test: QuitRequested complete' "$log" >/dev/null 2>&1 \
	|| fail "feed_server did not complete its clean shutdown path"
grep -F 'FileDaemon lifecycle: requests released' "$log" >/dev/null 2>&1 \
	|| fail "the downloader did not release all requests"
grep -F 'FeedServer lifecycle: services stopped' "$log" >/dev/null 2>&1 \
	|| fail "feed_server did not release its runtime services"

trap - EXIT HUP INT TERM

echo "Smoke test passed: feed_server started, loaded Atom and RSS, and shut down cleanly."
