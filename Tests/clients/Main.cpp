#include <libfeedkit/FeedKitConstants.h>

#include <Application.h>
#include <Messenger.h>
#include <OS.h>

#include <stdio.h>
#include <string.h>

using namespace FeedKit;

static bool
ServerIsRunning()
{
	return BMessenger(ServerSignature).IsValid();
}

int
main(int argc, char **argv)
{
	if (argc != 2 || (strcmp(argv[1], "status") != 0
		&& strcmp(argv[1], "stop") != 0)) {
		fprintf(stderr, "Usage: %s {status|stop}\n", argv[0]);
		return 2;
	}

	BApplication application("application/x-vnd.feedkit-client-test-control");
	if (strcmp(argv[1], "status") == 0)
		return ServerIsRunning() ? 0 : 1;

	BMessenger server(ServerSignature);
	if (!server.IsValid()) {
		fprintf(stderr, "feed_server is not running\n");
		return 1;
	}
	status_t status = server.SendMessage(B_QUIT_REQUESTED);
	if (status != B_OK) {
		fprintf(stderr, "could not request feed_server shutdown: %s\n",
			strerror(status));
		return 1;
	}

	for (int32 attempt = 0; attempt < 100; attempt++) {
		if (!ServerIsRunning())
			return 0;
		snooze(100000);
	}
	fprintf(stderr, "feed_server did not stop within 10 seconds\n");
	return 1;
}
