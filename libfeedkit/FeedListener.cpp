#include "FeedListener.h"
#include "FeedKitConstants.h"
#include "FeedHandler.h"
#include "Feed.h"
#include "Channel.h"
#include "ChannelSpecification.h"
#include "Item.h"
#include "ItemSpecification.h"
#include "ErrorDetails.h"
#include "DownloadProgress.h"

#include "Private/Constants.h"

#include <FindDirectory.h>
#include <List.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Roster.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

//#pragma mark Namespace import

using namespace FeedKit;

//#pragma mark Constants

const char *FeedKit::ServerSignature = "application/x-vnd.beclan.feed_server";

const bigtime_t kTimeOut = 100 * 1000;

//#pragma mark Functions

//#pragma mark Constructor

FeedListener::FeedListener(void)
	: fListening(false),
	fRegistered(false),
	fMsgr(FeedKit::ServerSignature) {
	
	Run();
	
	be_roster->StartWatching(BMessenger(this));
	
	fTarget = BMessenger(this);
};

FeedListener::~FeedListener(void) {
	be_roster->StopWatching(BMessenger(this));
	
	StopListening();
};

//#pragma mark BLooper Hooks

void FeedListener::MessageReceived(BMessage *msg) {
	switch (msg->what) {
		case B_SOME_APP_LAUNCHED: {
			const char *signature = NULL;
			if (msg->FindString("be:signature", &signature) != B_OK) return;
			
			if (strcmp(signature, FeedKit::ServerSignature) == 0) {
				fMsgr = BMessenger(FeedKit::ServerSignature);
				
				if ((fListening == true) && (fRegistered == false)) {
					AddListener(fTarget);
				};
								
				for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
					(*hIt)->ServerStarted();
				}
			};
		} break;
		
		case B_SOME_APP_QUIT: {
			const char *signature = NULL;
			if (msg->FindString("be:signature", &signature) != B_OK) return;
			
			if ((fRegistered == true) && (strcmp(signature, FeedKit::ServerSignature) == 0)) {
				fRegistered = false;
				
				for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
					(*hIt)->ServerShutdown();
				};
			};
		} break;
		
		case FeedKit::Private::FromServer::RegisterFeedComplete: {
			Feed feed;
			if (msg->FindFlat("feed", &feed) == B_OK) {
				for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
					(*hIt)->FeedRegistered(&feed);
				};
			};
		} break;
		
		case FeedKit::Private::FromServer::ChannelUpdated: {
			Feed feed;
			const char *channelUUID = NULL;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			
			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
						
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->ChannelUpdated(&feed, channel);
			};
		} break;

		case FeedKit::Private::FromServer::ItemRead: {
			Feed feed;
			const char *channelUUID = NULL;
			const char *itemUUID = NULL;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindString("item", &itemUUID) != B_OK) return;
			
			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
			Item *item = channel->ItemByUUID(itemUUID);
			if (item == NULL) return;
			item->ParentChannel(channel);
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->ItemRead(&feed, channel, item);
			};
		} break;
		
		case FeedKit::Private::FromServer::RegisterFeedError: {
			Feed feed;
			ErrorDetails error;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindFlat("error", &error) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedRegisteredError(&feed, &error);
			};
		} break;
		
		case FeedKit::Private::FromServer::DownloadFeedStarted: {
			Feed feed;
			DownloadProgress progress;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindFlat("download", &progress) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedDownloadStarted(&feed, &progress);
			};		
		} break;
		
		case FeedKit::Private::FromServer::DownloadFeedError: {
			Feed feed;
			DownloadProgress progress;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindFlat("download", &progress) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedDownloadError(&feed, &progress);
			};		
		} break;

		case FeedKit::Private::FromServer::DownloadFeedProgress: {
			Feed feed;
			DownloadProgress progress;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindFlat("download", &progress) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedDownloadProgress(&feed, &progress);
			};
		} break;

		case FeedKit::Private::FromServer::DownloadFeedFinished: {
			Feed feed;
			DownloadProgress progress;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindFlat("download", &progress) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedDownloadFinished(&feed, &progress);
			};
		} break;

		case FeedKit::Private::FromServer::DownloadEnclosureStarted: {
			Feed feed;
			const char *channelUUID = NULL;
			const char *itemUUID = NULL;
			const char *enclosureUUID = NULL;
					
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindString("item", &itemUUID) != B_OK) return;
			if (msg->FindString("enclosure", &enclosureUUID) != B_OK) return;
			
			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
			Item *item = channel->ItemByUUID(itemUUID);
			if (item == NULL) return;
			item->ParentChannel(channel);
			Enclosure *enclosure = item->EnclosureByUUID(enclosureUUID);
			if (enclosure == NULL) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->EnclosureDownloadStarted(&feed, item, enclosure);
			};
		} break;
		
		case FeedKit::Private::FromServer::DownloadEnclosureProgress: {
			Feed feed;
			const char *channelUUID = NULL;
			const char *itemUUID = NULL;
			const char *enclosureUUID = NULL;

			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindString("item", &itemUUID) != B_OK) return;
			if (msg->FindString("enclosure", &enclosureUUID) != B_OK) return;

			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
			Item *item = channel->ItemByUUID(itemUUID);
			if (item == NULL) return;
			item->ParentChannel(channel);
			Enclosure *enclosure = item->EnclosureByUUID(enclosureUUID);
			if (enclosure == NULL) return;

			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->EnclosureDownloadProgress(&feed, item, enclosure);
			};
		} break;
		
		case FeedKit::Private::FromServer::DownloadEnclosureFinished: {	
			Feed feed;
			const char *channelUUID = NULL;
			const char *itemUUID = NULL;
			const char *enclosureUUID = NULL;

			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindString("item", &itemUUID) != B_OK) return;
			if (msg->FindString("enclosure", &enclosureUUID) != B_OK) return;
			
			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
			Item *item = channel->ItemByUUID(itemUUID);
			if (item == NULL) return;
			item->ParentChannel(channel);
			Enclosure *enclosure = item->EnclosureByUUID(enclosureUUID);
			if (enclosure == NULL) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->EnclosureDownloadFinished(&feed, item, enclosure);
			};
		} break;
		
		case FeedKit::Private::FromServer::EnclosureDownloadStatusChanged: {
			Feed feed;
			const char *channelUUID = NULL;
			const char *itemUUID = NULL;
			const char *enclosureUUID = NULL;

			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindString("item", &itemUUID) != B_OK) return;
			if (msg->FindString("enclosure", &enclosureUUID) != B_OK) return;
			
			Channel *channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);
			Item *item = channel->ItemByUUID(itemUUID);
			if (item == NULL) return;
			item->ParentChannel(channel);
			Enclosure *enclosure = item->EnclosureByUUID(enclosureUUID);
			if (enclosure == NULL) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->EnclosureDownloadStatusChanged(&feed, item, enclosure);
			};
		} break;
		
		case FeedKit::Private::FromServer::ChannelIconUpdated: {
			Feed feed;
			const char *channelUUID = NULL;
			Channel *channel = NULL;
			entry_ref ref;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			if (msg->FindString("channel", &channelUUID) != B_OK) return;
			if (msg->FindRef("ref", &ref) != B_OK) return;
			channel = feed.ChannelByUUID(channelUUID);
			if (channel == NULL) return;
			channel->ParentFeed(&feed);

			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->ChannelIconUpdated(&feed, channel, ref);
			};
		} break;
		
		case FeedKit::Private::FromServer::FeedSubscriptionChanged: {
			Feed feed;
			
			if (msg->FindFlat("feed", &feed) != B_OK) return;
			
			for (handler_t::iterator hIt = fHandler.begin(); hIt != fHandler.end(); hIt++) {
				(*hIt)->FeedSubscriptionChanged(&feed);
			};
		} break;
		
		default: {
			BLooper::MessageReceived(msg);
		};
	};
};

status_t FeedListener::InitCheck(void) {
	status_t init = B_ERROR;
	if (fMsgr.IsValid() == true) init = B_OK;

	return init;
};


//#pragma mark Public
		
void FeedListener::StartListening(void) {
	if (fListening == false) {
		fListening = true;

		if (fMsgr.IsValid() == false) {
			status_t status = be_roster->Launch(FeedKit::ServerSignature);
			if (status == B_OK || status == B_ALREADY_RUNNING)
				fMsgr = BMessenger(FeedKit::ServerSignature);
		}

		if (fMsgr.IsValid() == true)
			AddListener(fTarget);
	};
};

void FeedListener::StopListening(void) {
	if (fListening == true) {
		RemoveListener(fTarget);
		fListening = false;
	}
};

status_t FeedListener::SendMessage(BMessage *msg, BMessage *reply) {
	if (msg == NULL) return B_BAD_VALUE;

	status_t status = B_ERROR;
	
	if (fMsgr.IsValid() == true) {
		if (reply) {
			status = fMsgr.SendMessage(msg, reply, kTimeOut, kTimeOut);
		} else {
			status = fMsgr.SendMessage(msg, (BHandler *)NULL, kTimeOut);
		};
	};
	
	return status;
};

bool FeedListener::ServerIsRunning(void) {
	BList teams;
	be_roster->GetAppList(FeedKit::ServerSignature, &teams);
	
	return teams.CountItems() > 0;
};

//#pragma mark Server Interaction Methods

status_t FeedListener::RefreshAllFeeds(void) {
	BMessage refresh(FeedKit::Private::ToServer::ForceRefresh);

	return SendMessage(&refresh);
};

status_t FeedListener::RefreshFeed(Feed *feed) {
	if (feed == NULL) return B_BAD_VALUE;

	BMessage refresh(FeedKit::Private::ToServer::ForceRefresh);
	refresh.AddString("url", feed->URL());
	
	return SendMessage(&refresh);
};

status_t FeedListener::MarkItemRead(Item *item) {
	if (item == NULL) return B_BAD_VALUE;

	BMessage markRead(FeedKit::Private::ToServer::MarkRead);
	markRead.AddString("item", item->UUID());
	
	return SendMessage(&markRead);
};

status_t FeedListener::RegisterFeed(const char *url, const char *name) {
	if (url == NULL) return B_BAD_VALUE;

	BMessage registerFeed(FeedKit::Private::ToServer::RegisterFeed);

	registerFeed.AddString("url", url);
	if (name != NULL) registerFeed.AddString("name", name);

	return SendMessage(&registerFeed);
};

status_t FeedListener::ChangeFeedSubscription(Feed *feed, bool subscribed) {
	if (feed == NULL) return B_BAD_VALUE;

	status_t result = B_OK;

	if (feed->Subscribed() != subscribed) {
		BMessage subscribeFeed(FeedKit::Private::ToServer::ChangeFeedSubscription);
		subscribeFeed.AddString("uuid", feed->UUID());
		subscribeFeed.AddString("url", feed->URL());
		subscribeFeed.AddBool("subscribed", subscribed);
		
		result = SendMessage(&subscribeFeed);
	};
	
	return result;
};

const char *FeedListener::ChannelIconPath(Channel *channel) {
	if (channel == NULL) return NULL;

	const char *path = NULL;
	fChannelIconPath = "";
	BMessage reply;
	BMessage getIcon(FeedKit::Private::ToServer::GetChannelIconPath);
	getIcon.AddString("channel", channel->UUID());

	SendMessage(&getIcon, &reply);	

	if (reply.what == FeedKit::Private::FromServer::ChannelIconPath) {
		if (reply.FindString("path", &path) == B_OK) {
			fChannelIconPath = path;
			return fChannelIconPath.String();
		}
	};
	
	return NULL;
};

feed_list_t FeedListener::Feeds(void) {
	feed_list_t feeds;
	
	BMessage getFeedList(FeedKit::Private::ToServer::GetFeedList);
	BMessage feedList;

	if (SendMessage(&getFeedList, &feedList) == B_OK) {	
		for (int32 i = 0; ; i++) {
			Feed *feed = new Feed();
			if (feedList.FindFlat("feed", i, feed) != B_OK) {
				delete feed;
				break;
			}
			feeds.push_back(feed);
		};
	};

	return feeds;
};

status_t FeedListener::DownloadEnclosure(Item *item, Enclosure *enclosure, const char *path) {
	if (item == NULL || enclosure == NULL || path == NULL) return B_BAD_VALUE;

	BMessage reply;
	BMessage downloadEnclosure(FeedKit::Private::ToServer::DownloadEnclosure);
	downloadEnclosure.AddString("item", item->UUID());
	downloadEnclosure.AddFlat("enclosure", enclosure);
	downloadEnclosure.AddString("path", path);
	
	status_t result = SendMessage(&downloadEnclosure, &reply);
	
	reply.PrintToStream();
	
	return result;
//	return SendMessage(&downloadEnclosure);
};

status_t FeedListener::CancelEnclosureDownload(Item *item, Enclosure *enclosure) {
	if (item == NULL || enclosure == NULL) return B_BAD_VALUE;

	BMessage cancelDownload(FeedKit::Private::ToServer::CancelEnclosureDownload);
	cancelDownload.AddString("item", item->UUID());
	cancelDownload.AddFlat("enclosure", enclosure);
	
	return SendMessage(&cancelDownload);
};

//#pragma mark Handler Subscription

void FeedListener::AddHandler(FeedHandler *handler) {
	if (handler != NULL
		&& std::find(fHandler.begin(), fHandler.end(), handler) == fHandler.end())
		fHandler.push_back(handler);
};

void FeedListener::RemoveHandler(FeedHandler *handler) {
	handler_t::iterator found = std::find(fHandler.begin(), fHandler.end(), handler);
	if (found != fHandler.end())
		fHandler.erase(found);
};

//#pragma mark Private

void FeedListener::AddListener(BMessenger target) {
	BMessage reply;
	BMessage msg(FeedKit::Private::AddListener);

	msg.AddMessenger("listener", target);
	fMsgr.SendMessage(&msg, &reply);
	
	if (reply.what == FeedKit::Private::Success) fRegistered = true;
};

void FeedListener::RemoveListener(BMessenger target) {
	BMessage reply;
	BMessage msg(FeedKit::Private::RemoveListener);
	
	msg.AddMessenger("listener", target);
	fMsgr.SendMessage(&msg, &reply);
	
	fRegistered = false;
};
