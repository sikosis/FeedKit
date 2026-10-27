#include <Application.h>
#include <Messenger.h>
#include <OS.h>
#include <String.h>

#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedHandler.h>
#include <libfeedkit/FeedListener.h>

#include <stdio.h>
#include <string.h>

using namespace FeedKit;

static const bigtime_t kServerTimeout = 10 * 1000000;
static const bigtime_t kChangeTimeout = 10 * 1000000;

class SubscriptionHandler : public FeedHandler {
	public:
		SubscriptionHandler(const char *url, bool subscribed)
			: fDone(create_sem(0, "FeedKit subscription result")),
			fURL(url),
			fSubscribed(subscribed)
		{
		}

		~SubscriptionHandler()
		{
			if (fDone >= B_OK)
				delete_sem(fDone);
		}

		void FeedSubscriptionChanged(Feed *feed)
		{
			if (fURL.ICompare(feed->URL()) == 0
				&& feed->Subscribed() == fSubscribed)
				release_sem(fDone);
		}

		status_t Wait()
		{
			return acquire_sem_etc(fDone, 1, B_RELATIVE_TIMEOUT,
				kChangeTimeout);
		}

	private:
		sem_id fDone;
		BString fURL;
		bool fSubscribed;
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

static void
DeleteFeeds(feed_list_t &feeds)
{
	for (feed_list_t::iterator it = feeds.begin(); it != feeds.end(); ++it)
		delete *it;
	feeds.clear();
}

int
main(int argc, char *argv[])
{
	if (argc != 3 || (strcmp(argv[2], "subscribe") != 0
		&& strcmp(argv[2], "unsubscribe") != 0)) {
		fprintf(stderr, "Usage: %s URL {subscribe|unsubscribe}\n", argv[0]);
		return 2;
	}

	bool subscribed = strcmp(argv[2], "subscribe") == 0;
	BApplication application("application/x-vnd.beclan.feedkit.feedsubscriber");
	FeedListener *listener = new FeedListener();
	SubscriptionHandler handler(argv[1], subscribed);
	listener->AddHandler(&handler);

	status_t status = StartListener(listener);
	feed_list_t feeds;
	Feed *match = NULL;
	if (status == B_OK) {
		feeds = listener->Feeds();
		for (feed_list_t::iterator it = feeds.begin(); it != feeds.end(); ++it) {
			if (BString(argv[1]).ICompare((*it)->URL()) == 0) {
				match = *it;
				break;
			}
		}
		if (match == NULL)
			status = B_NAME_NOT_FOUND;
	}

	if (status == B_OK && match->Subscribed() != subscribed) {
		status = listener->ChangeFeedSubscription(match, subscribed);
		if (status == B_OK)
			status = handler.Wait();
	}

	if (status == B_OK) {
		printf("Feed is %ssubscribed: %s\n", subscribed ? "" : "un",
			argv[1]);
	} else {
		fprintf(stderr, "Unable to change subscription for %s: %s\n",
			argv[1], strerror(status));
	}

	listener->RemoveHandler(&handler);
	DeleteFeeds(feeds);
	StopListener(listener);
	return status == B_OK ? 0 : 1;
}
