#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
server="$root_dir/build/feed_server"
rss="$root_dir/Tests/fixtures/integration.rss"
atom="$root_dir/Tests/fixtures/integration.atom"
missing="$root_dir/Tests/fixtures/does-not-exist.xml"
log="$root_dir/build/integration-test.log"
server_pid=

fail()
{
	echo "Integration test failed: $1" >&2
	if [ -f "$log" ]; then
		echo "Server output ($log):" >&2
		sed -n '1,260p' "$log" >&2
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

for artifact in "$server" "$root_dir/build/Parsers/Atom" \
	"$root_dir/build/Parsers/RSS" "$rss" "$atom"; do
	[ -f "$artifact" ] || fail "missing test input: $artifact"
done
[ ! -e "$missing" ] || fail "failure fixture unexpectedly exists: $missing"

LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

: > "$log"
echo "Starting local RSS/Atom registration and refresh test (log: $log)..."
FEEDKIT_INTEGRATION_TEST=1 "$server" --integration-test \
	"file://$rss" "file://$atom" "file://$missing" > "$log" 2>&1 &
server_pid=$!
echo "Waiting up to 20 seconds for registrations, forced refreshes, the expected failure, and shutdown..."

attempt=0
while kill -0 "$server_pid" 2>/dev/null && [ "$attempt" -lt 20 ]; do
	attempt=$((attempt + 1))
	sleep 1
done

if kill -0 "$server_pid" 2>/dev/null; then
	fail "feed_server did not complete integration mode within 20 seconds"
fi

if ! wait "$server_pid"; then
	server_pid=
	fail "feed_server returned a failure status"
fi
server_pid=

grep -F 'FeedServer integration test: FeedKit RSS integration fixture download 2 validated' \
	"$log" >/dev/null 2>&1 || fail "RSS registration and refresh were not both validated"
grep -F 'FeedServer integration test: FeedKit Atom integration fixture download 2 validated' \
	"$log" >/dev/null 2>&1 || fail "Atom registration and refresh were not both validated"
grep -F 'FeedServer integration test: expected download failure validated' \
	"$log" >/dev/null 2>&1 || fail "the missing-feed failure path was not validated"
grep -F 'FeedServer integration test passed: RSS and Atom registered and refreshed' \
	"$log" >/dev/null 2>&1 || fail "the server did not report integration success"
grep -F 'FileDaemon lifecycle: requests released' "$log" >/dev/null 2>&1 \
	|| fail "the downloader did not release all requests"
grep -F 'FeedServer lifecycle: services stopped' "$log" >/dev/null 2>&1 \
	|| fail "the server did not release its runtime services"

trap - EXIT HUP INT TERM

echo "Integration test passed: local RSS and Atom feeds registered, parsed, refreshed; a missing feed failed safely; and the server shut down cleanly."
