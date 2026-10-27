#ifndef FEEDINFOPOPPER_H
#define FEEDINFOPOPPER_H

#include <Application.h>

#include <libfeedkit/FeedHandler.h>

namespace FeedKit {
	class Channel;
	class Enclosure;
	class FeedListener;
	class Item;
}

class FeedInfoPopper : public BApplication, public FeedKit::FeedHandler {
	public:
							FeedInfoPopper(bool smokeTest = false);
							~FeedInfoPopper();

		void				ReadyToRun();

		void				FeedRegistered(FeedKit::Feed *feed);
		void				ChannelUpdated(FeedKit::Feed *feed,
								FeedKit::Channel *channel);
		void				EnclosureDownloadProgress(FeedKit::Feed *feed,
								FeedKit::Item *item,
								FeedKit::Enclosure *enclosure);

	private:
		void				BroadcastChannelContents(FeedKit::Channel *channel);
		void				BroadcastDownloadProgress(FeedKit::Enclosure *enclosure);
		void				StopListener();

		bool				fSmokeTest;
		FeedKit::FeedListener
							*fFeedListener;
};

#endif
