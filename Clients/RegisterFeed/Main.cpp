#include <Application.h>
#include <Messenger.h>
#include <OS.h>

#include <libfeedkit/ErrorDetails.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedHandler.h>
#include <libfeedkit/FeedListener.h>

#include <stdio.h>
#include <string.h>

using namespace FeedKit;

static const bigtime_t kServerTimeout = 10 * 1000000;
static const bigtime_t kRegistrationTimeout = 30 * 1000000;

class RegistrationHandler : public FeedHandler {
	public:
		RegistrationHandler()
			: fDone(create_sem(0, "FeedKit registration result")),
			fResult(B_ERROR)
		{
		}

		~RegistrationHandler()
		{
			if (fDone >= B_OK)
				delete_sem(fDone);
		}

		void FeedRegistered(Feed *feed)
		{
			printf("Registered feed: %s (%s)\n", feed->Name(), feed->URL());
			fResult = B_OK;
			release_sem(fDone);
		}

		void FeedRegisteredError(Feed *feed, ErrorDetails *error)
		{
			fprintf(stderr, "Unable to register %s: %s\n", feed->URL(),
				error->Message());
			fResult = B_ERROR;
			release_sem(fDone);
		}

		status_t Wait()
		{
			status_t status = acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT,
				kRegistrationTimeout);
			return status == B_OK ? fResult : status;
		}

	private:
		sem_id fDone;
		status_t fResult;
};

static status_t
StartListener(FeedListener *listener)
{
	listener->StartListening();
	for (int32 attempt = 0; attempt < 100; attempt++) {
		if (listener->InitCheck() == B_OK)
			return B_OK;
		snooze(kServerTimeout / 100);
	}
	return B_TIMED_OUT;
}

static void
StopListener(FeedListener *listener)
{
	thread_id thread = listener->Thread();
	listener->StopListening();
	BMessenger(listener).SendMessage(B_QUIT_REQUESTED);
	if (thread >= B_OK) {
		status_t result;
		wait_for_thread(thread, &result);
	}
}

int
main(int argc, char *argv[])
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: %s URL [friendly-name]\n", argv[0]);
		return 2;
	}

	BApplication application("application/x-vnd.beclan.feedkit.registerfeed");
	FeedListener *listener = new FeedListener();
	RegistrationHandler handler;
	listener->AddHandler(&handler);

	status_t status = StartListener(listener);
	if (status == B_OK)
		status = listener->RegisterFeed(argv[1], argc == 3 ? argv[2] : NULL);
	if (status == B_OK)
		status = handler.Wait();

	listener->RemoveHandler(&handler);
	StopListener(listener);
	if (status != B_OK) {
		fprintf(stderr, "RegisterFeed failed: %s\n", strerror(status));
		return 1;
	}
	return 0;
}
