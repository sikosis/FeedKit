#!/bin/sh

set -u

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
server="$root_dir/build/feed_server"
register="$root_dir/build/Clients/RegisterFeed"
subscriber="$root_dir/build/Clients/FeedSubscriber"
control="$root_dir/build/Tests/feedkit_client_test_control"
fixture="$root_dir/Tests/fixtures/integration.rss"
log="$root_dir/build/client-smoke-test.log"
register_log="$root_dir/build/client-register-test.log"
subscriber_log="$root_dir/build/client-subscriber-test.log"
server_pid=
store=

fail()
{
	echo "Client smoke test failed: $1" >&2
	for output in "$log" "$register_log" "$subscriber_log"; do
		if [ -s "$output" ]; then
			echo "Output ($output):" >&2
			sed -n '1,240p' "$output" >&2
		fi
	done
	exit 1
}

cleanup()
{
	if [ -n "$server_pid" ] && kill -0 "$server_pid" 2>/dev/null; then
		"$control" stop >/dev/null 2>&1 || kill "$server_pid" 2>/dev/null || true
		wait "$server_pid" 2>/dev/null || true
	fi
	case "$store" in
		"$root_dir"/build/client-store-*) rm -rf -- "$store" ;;
	esac
}

trap cleanup EXIT HUP INT TERM

[ "$(uname -s)" = Haiku ] || fail "this runtime check must be run on Haiku"
for artifact in "$server" "$register" "$subscriber" "$control" "$fixture"; do
	[ -f "$artifact" ] || fail "missing test input: $artifact"
done

LIBRARY_PATH="$root_dir/build${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LIBRARY_PATH

if "$control" status >/dev/null 2>&1; then
	fail "feed_server is already running; stop it before this isolated test"
fi

store=$(mktemp -d "$root_dir/build/client-store-XXXXXX") \
	|| fail "could not create the isolated client store"
: > "$log"
: > "$register_log"
: > "$subscriber_log"

echo "Starting feed_server with an isolated client store (log: $log)..."
"$server" --client-test "$store" > "$log" 2>&1 &
server_pid=$!

attempt=0
while ! "$control" status >/dev/null 2>&1 && [ "$attempt" -lt 10 ]; do
	if ! kill -0 "$server_pid" 2>/dev/null; then
		fail "feed_server exited during startup"
	fi
	attempt=$((attempt + 1))
	sleep 1
done
"$control" status >/dev/null 2>&1 \
	|| fail "feed_server did not become available within 10 seconds"

url="file://$fixture"
"$register" "$url" "FeedKit client smoke fixture" \
	> "$register_log" 2>&1 || fail "RegisterFeed could not register the fixture"
"$subscriber" "$url" unsubscribe \
	> "$subscriber_log" 2>&1 || fail "FeedSubscriber could not unsubscribe the fixture"
"$subscriber" "$url" subscribe \
	>> "$subscriber_log" 2>&1 || fail "FeedSubscriber could not resubscribe the fixture"

"$control" stop || fail "could not request a clean server shutdown"
if ! wait "$server_pid"; then
	server_pid=
	fail "feed_server returned a failure status"
fi
server_pid=

grep -F 'Registered feed: FeedKit client smoke fixture' "$register_log" \
	>/dev/null 2>&1 || fail "RegisterFeed did not report completion"
grep -F 'FeedServer client test: isolated store ready' "$log" \
	>/dev/null 2>&1 || fail "feed_server did not prepare the isolated store"
grep -F "Feed is unsubscribed: $url" "$subscriber_log" \
	>/dev/null 2>&1 || fail "unsubscribe completion was not reported"
grep -F "Feed is subscribed: $url" "$subscriber_log" \
	>/dev/null 2>&1 || fail "subscribe completion was not reported"
grep -F 'FileDaemon lifecycle: requests released' "$log" >/dev/null 2>&1 \
	|| fail "the downloader did not release all requests"
grep -F 'FeedServer lifecycle: services stopped' "$log" >/dev/null 2>&1 \
	|| fail "feed_server did not release its runtime services"

cleanup
trap - EXIT HUP INT TERM

echo "Client smoke test passed: RegisterFeed registered the local fixture, FeedSubscriber toggled its subscription, and the isolated server shut down cleanly."
