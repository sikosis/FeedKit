#ifndef TWINDOW_H
#define TWINDOW_H

#include <String.h>
#include <Window.h>

#include <libfeedkit/FeedHandler.h>
#include <libfeedkit/FeedKitConstants.h>

#include <vector>

class BButton;
class BListView;
class BTextView;

namespace FeedKit {
	class Channel;
	class FeedListener;
	class Item;
}

class TWindow : public BWindow, public FeedKit::FeedHandler {
	public:
							TWindow(bool smokeTest = false);
							~TWindow();

		bool				QuitRequested();
		void				MessageReceived(BMessage *message);

		void				ServerStarted();
		void				ServerShutdown();
		void				FeedRegistered(FeedKit::Feed *feed);
		void				FeedSubscriptionChanged(FeedKit::Feed *feed);
		void				ChannelUpdated(FeedKit::Feed *feed,
								FeedKit::Channel *channel);
		void				ItemRead(FeedKit::Feed *feed,
								FeedKit::Channel *channel, FeedKit::Item *item);

	private:
		void				BuildInterface();
		void				StartListener();
		void				StopListener();
		void				ClearFeeds();
		void				ReloadFeeds();
		void				ReloadItems();
		void				DisplayItem();
		void				OpenSelectedLink();

		bool				fSmokeTest;
		FeedKit::FeedListener
							*fFeedListener;
		FeedKit::feed_list_t	fFeeds;
		std::vector<FeedKit::Channel *>
							fChannels;
		std::vector<FeedKit::Item *>
							fItems;
		BListView			*fChannelList;
		BListView			*fItemList;
		BTextView			*fContentView;
		BButton				*fOpenButton;
		BButton				*fMarkReadButton;
		BString				fSelectedLink;
};

#endif
