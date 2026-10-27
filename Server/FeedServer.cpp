#include "FeedServer.h"
#include "FeedServerConstants.h"
#include "Common/IMKitUtilities.h"
#include "Common/SettingsFile.h"
#include "Common/QueryLooper.h"
#include "ParserManager.h"
#include "EnclosureRequestHandler.h"
#include "LocalEnclosureDownload.h"
#include "FileDaemon.h"
#include "FileRequest.h"
#include "ObjectUpdateFilter.h"

#include <libfeedkit/Private/Constants.h>

#include <Debug.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <kernel/fs_index.h>
#include <kernel/fs_info.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Roster.h>
#include <String.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <unistd.h>

#include <errno.h>
#include <limits.h>
#include <set>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <libfeedkit/FeedKitConstants.h>
#include <libfeedkit/Channel.h>
#include <libfeedkit/DownloadProgress.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/Item.h>
#include <libfeedkit/SettingsManager.h>
#include <libfeedkit/ErrorDetails.h>

using namespace FeedKit;
using namespace FeedKit::Settings;

//#pragma mark Types

typedef std::map<BString, Channel *> titlechannel_t;
typedef std::map<BString, Channel *> channel_cache_t;

class DownloadTerminalReply {
	public:
		explicit DownloadTerminalReply(BMessage *message)
			: fMessage(message) {
		}

		~DownloadTerminalReply() {
			BMessage reply(B_REPLY);
			fMessage->SendReply(&reply);
		}

	private:
		BMessage *fMessage;
};

static void
DeleteUnqueuedRequest(FileRequest *request, BMessenger *target,
	BMessage *start, BMessage *finish, BMessage *error,
	BMessage *cancel = NULL)
{
	delete request;
	delete target;
	delete start;
	delete finish;
	delete error;
	delete cancel;
}

//#pragma mark Constants

const char *kSettingsDir = "feed_kit";
const char *kSettingsFile = "preferences";
const int32 kSecond = 1000000;
const int32 kMinute = 60 * kSecond;
const int32 kDefaultRefresh = 30 * kMinute;
const int32 kDownloadStatusInterval = kSecond * 1; // XXX ?
const char *kPredicateChannel = "((feed:url==*)&&(BEOS:TYPE==\"text/x-feed-channel\"))";
const char *kPredicateEnclosure = "(feedkit:enclosure_uuid==*)";
const int32 kMaxDownloadDaemonThreads = 5;
const int32 kFlattenFeedInterval = 10 * kMinute;

// Message whats

const int32 kMsgQueryChannel = 'fsqc';
const int32 kMsgQueryEnclosure = 'fsqe';
const int32 kMsgFeedDownloadStart = 'ffds';
const int32 kMsgFeedDownloadFinish = 'ffdf';
const int32 kMsgFeedDownloadError = 'ffde';
const int32 kMsgCheckDownloads = 'fscd';
const int32 kMsgEnclosureDownloadStart = 'feds';
const int32 kMsgEnclosureDownloadFinish = 'fedf';
const int32 kMsgEnclosureDownloadError = 'fede';
const int32 kMsgEnclosureDownloadCancelled = 'fedc';
const int32 kMsgChannelIconDownloadStart = 'fids';
const int32 kMsgChannelIconDownloadFinish = 'fidf';
const int32 kMsgChannelIconDownloadError = 'fide';
const int32 kMsgFlattenFeeds = 'flfe';
const int32 kMsgIntegrationTimeout = 'fitt';
const bigtime_t kIntegrationTimeout = 15 * kSecond;

//#pragma mark Constructor

#if 0
void PrintFeed(Feed *feed) {
	printf("%s (%s)\n", feed->DisplayName(), feed->UUID());
	for (int32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		
		printf("  %i: %s (%s)\n", i, channel->Title(), channel->UUID());
		
		for (int32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
			
//			printf("    %i: %s (%s, %s, %s) (%s)\n", j, item->GUID(),
//				item->Current() ? "current" : "archived", item->New() ? "new" : "old",
//				item->Read() ? "read" : "unread", item->UUID());
			printf("    %i: %s (%s)\n", j, item->Title(),item->UUID());

			for (int32 k = 0; k < item->ContentCount(); k++) {
				Content *content = item->ContentAt(k);
				
				printf("      %i: %s (%s)\n", k, content->MIMEType(), content->Summary());
			};
		};
	};
};
#endif

FeedServer::FeedServer(bool smokeTest, const char *integrationRSSURL,
	const char *integrationAtomURL, const char *integrationFailureURL,
	const char *integrationParseFailureURL, const char *clientTestStore)
	: BApplication(smokeTest ? "application/x-vnd.beclan.feed_server.smoke"
		: integrationRSSURL != NULL
			? "application/x-vnd.beclan.feed_server.integration" : ServerSignature),
	 fSmokeTest(smokeTest),
	 fIntegrationTest(integrationRSSURL != NULL && integrationAtomURL != NULL),
	 fClientTest(clientTestStore != NULL && clientTestStore[0] != '\0'),
	 fIntegrationTestFailed(false),
	 fIntegrationTestComplete(false),
	 fCurlInitialized(false),
	 fServicesStopped(false),
	 fIntegrationRSSURL(integrationRSSURL == NULL ? "" : integrationRSSURL),
	 fIntegrationAtomURL(integrationAtomURL == NULL ? "" : integrationAtomURL),
	 fIntegrationFailureURL(integrationFailureURL == NULL ? "" : integrationFailureURL),
	 fIntegrationFailureRequested(false),
	 fIntegrationParseFailureURL(integrationParseFailureURL == NULL
		? "" : integrationParseFailureURL),
	 fIntegrationParseFailureRequested(false),
	 fIntegrationPersistence(getenv("FEEDKIT_INTEGRATION_STORE") != NULL
		&& getenv("FEEDKIT_INTEGRATION_STORE")[0] != '\0'),
	 fIntegrationRestart(fIntegrationPersistence
		&& getenv("FEEDKIT_INTEGRATION_RESTART") != NULL),
	 fIntegrationTimeoutRunner(NULL),
	 fFeedQuery(NULL),
	 fEnclosureQuery(NULL),
	 fParserManager(NULL),
	 fFeedDaemon(NULL),
	 fSettings(NULL),
	 fRefresh(kDefaultRefresh),
	 fDownloadStatusRunner(NULL),
	 fFlattenFeedRunner(NULL),
	 fVolRoster(NULL) {
	if (fClientTest)
		fSettingsPath.SetTo(clientTestStore);
	else if (fIntegrationPersistence)
		fSettingsPath.SetTo(getenv("FEEDKIT_INTEGRATION_STORE"));

	AddCommonFilter(new ObjectUpdateFilter(this));
	
	// Set up the BMessage handlers
	fHandler[kMsgCheckDownloads] = &FeedServer::HandleCheckDownloads;
	fHandler[kMsgQueryChannel] = &FeedServer::HandleChannelQuery;
	fHandler[kMsgQueryEnclosure] = &FeedServer::HandleEnclosureQuery;
	fHandler[kMsgFeedDownloadStart] = &FeedServer::HandleFeedDownloadStart;
	fHandler[kMsgFeedDownloadError] = &FeedServer::HandleFeedDownloadError;
	fHandler[kMsgFeedDownloadFinish] = &FeedServer::HandleFeedDownloadFinish;
	fHandler[FeedKit::Private::ToServer::ForceRefresh] = &FeedServer::HandleForceRefresh;
	fHandler[FeedKit::Private::ToServer::MarkRead] = &FeedServer::HandleMarkRead;
	fHandler[FeedKit::Private::ToServer::DownloadEnclosure] = &FeedServer::HandleDownloadEnclosure;
	fHandler[FeedKit::Private::ToServer::CancelEnclosureDownload] = &FeedServer::HandleCancelEnclosureDownload;
	fHandler[kMsgEnclosureDownloadCancelled] = &FeedServer::HandleEnclosureDownloadCancelled;
	fHandler[kMsgEnclosureDownloadStart] = &FeedServer::HandleEnclosureDownloadStart;
	fHandler[kMsgEnclosureDownloadFinish] = &FeedServer::HandleEnclosureDownloadFinish;
	fHandler[kMsgEnclosureDownloadError] = &FeedServer::HandleEnclosureDownloadError;
	fHandler[FeedKit::Private::ToServer::RegisterFeed] = &FeedServer::HandleRegisterFeed;
	fHandler[FeedKit::Private::ToServer::GetFeedList] = &FeedServer::HandleGetFeedList;
	fHandler[FeedKit::Private::ToServer::GetChannelIconPath] = &FeedServer::HandleGetChannelIconPath;
	fHandler[FeedKit::Private::AddListener] = &FeedServer::HandleAddListener;
	fHandler[FeedKit::Private::RemoveListener] = &FeedServer::HandleRemoveListener;
	fHandler[FeedKit::Private::SettingsTemplateUpdated] = &FeedServer::HandleSettingsTemplateUpdated;
	fHandler[B_NODE_MONITOR] = &FeedServer::HandleNodeMonitor;
	fHandler[kMsgChannelIconDownloadStart] = &FeedServer::HandleChannelIconDownloadStart;
	fHandler[kMsgChannelIconDownloadFinish] = &FeedServer::HandleChannelIconDownloadFinish;
	fHandler[kMsgChannelIconDownloadError] = &FeedServer::HandleChannelIconDownloadError;
	fHandler[FeedKit::Private::ToServer::ChangeFeedSubscription] = &FeedServer::HandleChangeFeedSubscription;
	fHandler[kMsgFlattenFeeds] = &FeedServer::HandleFlattenFeeds;
	fHandler[kMsgIntegrationTimeout] = &FeedServer::HandleIntegrationTimeout;

	fRefFeedUUID.clear();
	fRefFeeds.clear();
	fURLFeeds.clear();
	fRefreshRunners.clear();
	
	fFeedUUID.clear();
	fChannelUUID.clear();
	fItemUUID.clear();

	if (fIntegrationTest || fClientTest) {
		fRefresh = kDefaultRefresh;
	} else {
		fSettings = new SettingsFile(kSettingsFile, kSettingsDir);
		if (fSettings->InitCheck() != B_OK) {
			fprintf(stderr, "FeedKit: Error loading preferences");
		};

		fSettings->Load();

		if (fSettings->FindInt32("refresh", &fRefresh) != B_OK) {
			fRefresh = kDefaultRefresh;
			fSettings->AddInt32("refresh", fRefresh);

			fSettings->Save();
		};
	}
	
	fFeedDaemon = new FileDaemon(kMaxDownloadDaemonThreads);
	fParserManager = new ParserManager();
};

FeedServer::~FeedServer(void) {
	ShutdownServices();

	if (!fIntegrationTest)
		SaveFeeds();

	std::set<Feed *> feeds;
	for (feed_uuid_t::iterator fIt = fFeedUUID.begin();
		fIt != fFeedUUID.end(); fIt++)
		feeds.insert(fIt->second);
	for (urlfeed_t::iterator uIt = fURLFeeds.begin();
		uIt != fURLFeeds.end(); uIt++)
		feeds.insert(uIt->second);

	fRefFeeds.clear();
	fRefFeedUUID.clear();
	fURLFeeds.clear();
	fFeedUUID.clear();
	fChannelUUID.clear();
	fItemUUID.clear();
	fEnclosureUUID.clear();
	for (std::set<Feed *>::iterator fIt = feeds.begin();
		fIt != feeds.end(); fIt++)
		delete *fIt;
		
	delete fParserManager;
	delete fSettings;
};

//#pragma mark BApplication Hooks
					
void FeedServer::MessageReceived(BMessage *msg) {
	int32 what = msg->what;
	eventhandler_t::iterator hIt = fHandler.find(what);
	if (hIt != fHandler.end()) {
		EventHandler handler = hIt->second;
		status_t result = (this->*handler)(msg);

		if (result != B_OK) {
			fprintf(stderr, "FeedServer::MessageReceived(%4.4s) returned %s\n", (char *)&what, strerror(result));
		};
	} else {
		BApplication::MessageReceived(msg);
	};
};


void FeedServer::ReadyToRun(void) {
	printf("FeedServer::ReadyToRun()\n");
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: entering ReadyToRun\n");

	BMessage flattenFeeds(kMsgFlattenFeeds);
	fFlattenFeedRunner = new BMessageRunner(BMessenger(this),
		&flattenFeeds, kFlattenFeedInterval);
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: flatten runner ready\n");

	CURLcode curlStatus = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (curlStatus != CURLE_OK) {
		fprintf(stderr, "FeedServer::ReadyToRun(): curl initialization failed: %s\n",
			curl_easy_strerror(curlStatus));
		if (fIntegrationTest)
			FailIntegrationTest("curl initialization failed");
		else
			PostMessage(B_QUIT_REQUESTED);
		return;
	}
	fCurlInitialized = true;

	if (fIntegrationTest) {
		BMessage integrationTimeout(kMsgIntegrationTimeout);
		fIntegrationTimeoutRunner = new BMessageRunner(BMessenger(this),
			&integrationTimeout, kIntegrationTimeout, 1);
		if (fIntegrationRestart) {
			RunIntegrationRestartTest();
			return;
		}

		if (fIntegrationPersistence) {
			status_t createStatus = create_directory(fSettingsPath.Path(), 0755);
			if (createStatus != B_OK && createStatus != B_FILE_EXISTS) {
				FailIntegrationTest("temporary persistence directory could not be created");
				return;
			}
		}

		fprintf(stderr, "FeedServer integration test: registering RSS and Atom fixtures\n");

		BMessage rss(FeedKit::Private::ToServer::RegisterFeed);
		rss.AddString("url", fIntegrationRSSURL.String());
		rss.AddString("name", "FeedKit RSS integration fixture");
		PostMessage(&rss);

		BMessage atom(FeedKit::Private::ToServer::RegisterFeed);
		atom.AddString("url", fIntegrationAtomURL.String());
		atom.AddString("name", "FeedKit Atom integration fixture");
		PostMessage(&atom);
		return;
	}

	if (fClientTest) {
		status_t result = create_directory(fSettingsPath.Path(), 0755);
		if (result != B_OK && result != B_FILE_EXISTS) {
			fprintf(stderr,
				"FeedServer client test: unable to prepare isolated store: %s\n",
				strerror(result));
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
	} else if (find_directory(B_USER_SETTINGS_DIRECTORY, &fSettingsPath) == B_OK) {
		BPath watchPath(fSettingsPath);
		watchPath.Append("BeClan/FeedKit");

		status_t result = create_directory(watchPath.Path(), 0755);
		if (result == B_OK || result == B_FILE_EXISTS) {
			BDirectory directory(watchPath.Path());
			node_ref node;
			result = directory.GetNodeRef(&node);
			if (result == B_OK)
				result = watch_node(&node, B_WATCH_DIRECTORY, be_app_messenger);
		}

		if (result != B_OK && result != B_FILE_EXISTS) {
			fprintf(stderr,
				"FeedServer::ReadyToRun(): Unable to prepare settings directory: %s\n",
				strerror(result));
		}
	} else {
		fprintf(stderr, "FeedServer::ReadyToRun(): Unable to find settings path\n");
	};

	if (!fClientTest)
		CheckIndexes();
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: indexes checked\n");

	BMessage checkDownloads(kMsgCheckDownloads);
	fDownloadStatusRunner = new BMessageRunner(BMessenger(this), &checkDownloads,
		kDownloadStatusInterval);
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: download runner ready\n");

	volume_t volumes;
	if (!fClientTest) {
		fVolRoster = new BVolumeRoster();
		fVolRoster->StartWatching(BMessenger(this));

		BVolume vol;
		fVolRoster->Rewind();
		while (fVolRoster->GetNextVolume(&vol) == B_OK) {
			if (vol.InitCheck() != B_OK || !vol.KnowsQuery()
				|| !vol.KnowsMime() || !vol.KnowsAttr())
				continue;
			volumes.push_back(vol);
		}
	}

	BMessage channelQueryMessage(kMsgQueryChannel);
	fFeedQuery = new QueryLooper(kPredicateChannel, volumes, "Feed Query", this,
		&channelQueryMessage);
	BMessage enclosureQueryMessage(kMsgQueryEnclosure);
	fEnclosureQuery = new QueryLooper(kPredicateEnclosure, volumes, "Enclosure Query", this,
		&enclosureQueryMessage);

	// Create the flattened feed directory. BDirectory::CreateDirectory() does
	// not create missing parent components, which matters for isolated stores.
	BPath path(fSettingsPath);
	status_t storageStatus = path.Append("BeClan");
	if (storageStatus == B_OK)
		storageStatus = path.Append("FeedKit");
	if (storageStatus == B_OK)
		storageStatus = path.Append("Feed");
	if (storageStatus == B_OK)
		storageStatus = path.Append("Data");
	if (storageStatus == B_OK)
		storageStatus = create_directory(path.Path(), 0755);
	if (storageStatus != B_OK && storageStatus != B_FILE_EXISTS) {
		fprintf(stderr, "FeedServer::ReadyToRun(): Unable to prepare feed storage: %s\n",
			strerror(storageStatus));
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK) {
		fprintf(stderr, "FeedServer::ReadyToRun(): Feed storage is unavailable: %s\n",
			strerror(dir.InitCheck()));
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
		
	// Iterate over the feeds
	entry_ref ref;
	while (dir.GetNextRef(&ref) == B_OK) {
		BEntry entry(&ref);
		// Ensure we're not attempting to launch the ChannelIcons directory
		if (entry.IsDirectory() == false) {
			LaunchFeed(ref);
		};
	};

	// Load previously saved icons
	storageStatus = path.Append("ChannelIcons");
	if (storageStatus == B_OK)
		storageStatus = create_directory(path.Path(), 0755);
	if (storageStatus != B_OK && storageStatus != B_FILE_EXISTS) {
		fprintf(stderr, "FeedServer::ReadyToRun(): Unable to prepare channel icon storage: %s\n",
			strerror(storageStatus));
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	dir.SetTo(path.Path());
		
	// Iterate over channel icons
	while (dir.GetNextRef(&ref) == B_OK) {
		BPath path(&ref);
		printf("Storing channel icon %s => %s\n", ref.name, path.Path());
		fChannelIcon[ref.name] = path.Path();
	};

	if (!fClientTest)
		StartAutoStartClients();
	else
		fprintf(stderr, "FeedServer client test: isolated store ready\n");

	if (fSmokeTest) {
		fprintf(stderr, "FeedServer smoke test: ReadyToRun complete\n");
		PostMessage(B_QUIT_REQUESTED);
	}
};

//#pragma mark BLooper Hooks

bool FeedServer::QuitRequested(void) {
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: clean shutdown requested\n");

	if (!fIntegrationTest && !fClientTest)
		StopAutoStartClients();

	ShutdownServices();
	if (fSmokeTest)
		fprintf(stderr, "FeedServer smoke test: QuitRequested complete\n");

	return BApplication::QuitRequested();
};

bool FeedServer::IntegrationTestPassed(void) const {
	return fIntegrationTest && fIntegrationTestComplete
		&& !fIntegrationTestFailed;
};

void FeedServer::ShutdownServices(void) {
	if (fServicesStopped)
		return;
	fServicesStopped = true;
	bool downloadsStopped = true;

	delete fDownloadStatusRunner;
	fDownloadStatusRunner = NULL;
	delete fFlattenFeedRunner;
	fFlattenFeedRunner = NULL;
	delete fIntegrationTimeoutRunner;
	fIntegrationTimeoutRunner = NULL;

	for (refresh_t::iterator reIt = fRefreshRunners.begin();
		reIt != fRefreshRunners.end(); reIt++) {
		delete reIt->second;
	}
	fRefreshRunners.clear();

	if (fVolRoster != NULL) {
		fVolRoster->StopWatching();
		delete fVolRoster;
		fVolRoster = NULL;
	}

	if (fFeedQuery != NULL) {
		if (fFeedQuery->Lock())
			fFeedQuery->Quit();
		fFeedQuery = NULL;
	}
	if (fEnclosureQuery != NULL) {
		if (fEnclosureQuery->Lock())
			fEnclosureQuery->Quit();
		fEnclosureQuery = NULL;
	}

	if (fFeedDaemon != NULL) {
		status_t result = fFeedDaemon->Shutdown();
		if (result != B_OK) {
			downloadsStopped = false;
			fprintf(stderr, "FeedServer::ShutdownServices(): downloader shutdown failed: %s\n",
				strerror(result));
		}
		fFeedDaemon = NULL;
	}

	if (fCurlInitialized && downloadsStopped) {
		curl_global_cleanup();
		fCurlInitialized = false;
	}

	if (downloadsStopped)
		fprintf(stderr, "FeedServer lifecycle: services stopped\n");
	else
		fprintf(stderr, "FeedServer lifecycle: service shutdown incomplete\n");
};

//#pragma mark ObjectSource Hooks

Feed *FeedServer::GetFeed(const char *uuid) {
	Feed *feed = NULL;
	feed_uuid_t::iterator fit = fFeedUUID.find(uuid);
	if (fit != fFeedUUID.end()) {
		feed = fit->second;
	}
	printf("GetFeed(%s)\n", uuid);
//	ASSERT_WITH_MESSAGE(feed != NULL, "FeedServer::GetFeed() is returning NULL");
		
	return feed;
};

Channel *FeedServer::GetChannel(const char *uuid) {
	Channel *channel = NULL;
	
	channel_uuid_t::iterator cIt = fChannelUUID.find(uuid);
	if (cIt != fChannelUUID.end()) {
		channel = cIt->second;
	};
	printf("GetChannel(%s)\n", uuid);
//	ASSERT_WITH_MESSAGE(channel != NULL, "FeedServer::GetChannel() is returning NULL");
	
	return channel;
};

Item *FeedServer::GetItem(const char *uuid) {
	Item *item = NULL;
	
	item_uuid_t::iterator iIt = fItemUUID.find(uuid);
	if (iIt != fItemUUID.end()) {
		item = iIt->second;
	};
	
	printf("GetItem(%s)\n", uuid);
//	ASSERT_WITH_MESSAGE(item != NULL, "FeedServer::GetItem() is returning NULL");
	
	return item;
};

Enclosure *FeedServer::GetEnclosure(const char *uuid) {
	Enclosure *enclosure = NULL;
	
	enclosure_uuid_t::iterator eIt = fEnclosureUUID.find(uuid);
	if (eIt != fEnclosureUUID.end()) {
		enclosure = eIt->second;
	};
	
	if (enclosure == NULL) {
		printf("GetEnclosure(%s)\n", uuid);

		for (eIt = fEnclosureUUID.begin(); eIt != fEnclosureUUID.end(); eIt++) {
			printf("\t%s: [%p]\n", eIt->first.String(), eIt->second);
		};
	}
	
//	ASSERT_WITH_MESSAGE(enclosure != NULL, "FeedServer::GetEnclosure() is returning NULL");
	
	return enclosure;
};
 
//#pragma mark Public

void FeedServer::UpdateEnclosureProgress(FileRequest *req, Enclosure *enc, double downloaded) {

//debugger("Beep");

	fprintf(stderr, "FeedServer::UpdateEnclosureProgress([%p], [%p], %.2f) called\n",
		req, enc, downloaded);

	ASSERT(enc != NULL);
	DownloadProgress *progress = ProgressFromRequest(req);
	fprintf(stderr, "Copy: %p\n", progress);

	enc->SetState(Downloading);
	
	fprintf(stderr, "Set state called\n");
		
	enc->SetProgress(progress);
	delete progress;
	
	fprintf(stderr, "SetProgress called\n");
};

//#pragma mark Private

status_t FeedServer::LaunchFeed(entry_ref ref, bool syndicate) {
	BFile file(&ref, B_READ_ONLY);
	off_t size = B_ERROR;
	char *buffer = NULL;
	Feed *feed = NULL;
	status_t error = B_ERROR;
	
	status_t sizeStatus = file.GetSize(&size);
	if (file.InitCheck() != B_OK || sizeStatus != B_OK || size <= 0
		|| size > INT32_MAX) {
		fprintf(stderr, "FeedServer::LaunchFeed(%s): Invalid feed file\n", ref.name);
		return sizeStatus == B_OK ? B_BAD_DATA : sizeStatus;
	}
	buffer = (char *)calloc(size, sizeof(char));
	if (buffer == NULL)
		return B_NO_MEMORY;

	error = file.Read(buffer, size);
	
	if (error != size) {
		fprintf(stderr, "FeedServer::LaunchFeed(%s): Could not read entire file (%i bytes) - aborting: %s\n", ref.name, size, strerror(error));
		free(buffer);
		
		return error;
	};

	feed = new Feed();
	error = feed->Unflatten(feed->TypeCode(), buffer, size);

	if (error != B_OK) {
		fprintf(stderr, "FeedServer::LaunchFeed(%s): Error unflattening Feed: %s\n", ref.name, strerror(error));
		free(buffer);
		delete feed;
		
		return error;
	};
	
	fURLFeeds[feed->URL()] = feed;
	fFeedUUID[feed->UUID()] = feed;
	fRefFeeds[ref] = feed;
	fRefFeedUUID[feed->UUID()] = ref;

	// Create parent / child pointers
	for (uint32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		
		for (uint32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
			item->ParentChannel(channel);
		};
		
		channel->ParentFeed(feed);		
	};

	UpdateFeedUUIDs(feed);
	UpdateReferences(feed);

	free(buffer);

	// Only re-syndicate the URL if we're subscribed to it
	if (syndicate && feed->Subscribed() == true)
		SyndicateURL(feed->URL(), feed->Name());
	return B_OK;
};

status_t FeedServer::SyndicateURL(const char *url, const char *name) {
	printf("FeedServer::SyndicateURL(%s, %s)\n", url, name);

	// If there's no refresh runner for this URL, set up a new one for the default interval -
	// when the download is successful a new runner will be created with the proper interval.
	// This covers the scenario where the initial download is not successful
	refresh_t::iterator rIt = fRefreshRunners.find(url);
	if (rIt == fRefreshRunners.end()) {
		// Set the feed up to be refreshed
		BMessage refreshMsg(FeedKit::Private::ToServer::ForceRefresh);
		refreshMsg.AddString("url", url);
		BMessageRunner *refresh = new BMessageRunner(BMessenger(this), &refreshMsg,
			kDefaultRefresh);
			
		fRefreshRunners[url] = refresh;
	};

	BMessage progress;
	urlfeed_t::iterator uIt = fURLFeeds.find(url);
	
	if (uIt != fURLFeeds.end()) {
		progress.what = FeedKit::Private::FromServer::DownloadFeedProgress;
		progress.AddFlat("feed", uIt->second);
	};

	FileRequest *request = new FileRequest(url, progress);

	BMessage *start = new BMessage(kMsgFeedDownloadStart);
	start->AddString("url", url);
	start->AddString("name", name);
	start->AddPointer("filerequest", request);
	
	BMessage *complete = new BMessage(kMsgFeedDownloadFinish);
	complete->AddString("url", url);
	complete->AddString("name", name);
	complete->AddPointer("filerequest", request);
	
	BMessage *error = new BMessage(kMsgFeedDownloadError);
	error->AddString("url", url);
	error->AddString("name", name);
	error->AddPointer("filerequest", request);
	
	BMessenger *target = new BMessenger(this);
	status_t result = fFeedDaemon->AddRequest(request, 0, target, start,
		complete, error);
	if (result != B_OK)
		DeleteUnqueuedRequest(request, target, start, complete, error);

	return result;
};


DownloadProgress *FeedServer::ProgressFromRequest(FileRequest *request) {
	DownloadProgress *progress = new DownloadProgress(request->MIME(), request->StartTime(), request->ExpectedSize(),
		request->Size(), request->EndTime());

	fprintf(stderr, "FeedServer::ProgressFromRequest(%p) - %i bytes downloaded\n", request, request->Size());
		
	return progress;
};

void FeedServer::MarkItems(Feed *feed, bool isCurrent, bool isNew) {
	uint32 channels = feed->ChannelCount();
	
	for (uint32 i = 0; i < channels; i++) {
		Channel *channel = feed->ChannelAt(i);
		uint32 items = channel->ItemCount();
		
		for (uint32 j = 0; j < items; j++) {
			Item *item = channel->ItemAt(j);
			if (item != NULL) {
				item->SetCurrent(isCurrent);
				item->SetNew(isNew);
			};
		};
	};
};

void FeedServer::Broadcast(BMessage *msg) {
	ObjectUpdateFilter filter(this);
	filter.FlattenDelayedObjects(msg);

	for (listener_t::const_iterator iIt = fListeners.begin(); iIt != fListeners.end(); iIt++) {
		(*iIt).SendMessage(msg);
	}
};

void FeedServer::AddListener(BMessenger msgr) {
	fListeners.push_back(msgr);
};

void FeedServer::RemoveListener(BMessenger msgr) {
	for (listener_t::iterator iIt = fListeners.begin(); iIt != fListeners.end(); iIt++) {
		if ((*iIt) == msgr) {
			fListeners.erase(iIt);
			break;
		};
	};
};

void FeedServer::StartAutoStartClients(void) {
	fprintf(stderr, "FeedServer::StartAutoStartClients(): Started\n");

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("BeClan/FeedKit/");
		
		BDirectory rootDir(settingsPath.Path());
		entry_ref ref;

		while (rootDir.GetNextRef(&ref) == B_OK) {
			BDirectory dir(&ref);
			if (dir.InitCheck() != B_OK) continue;
			if (dir.CountEntries() == 0) continue;
			if (dir.Contains("Templates", B_DIRECTORY_NODE) == false) continue;

			BDirectory templateDir(&dir, "Templates");
			if (templateDir.InitCheck() != B_OK) continue;
			if (templateDir.CountEntries() == 0) continue;
			
			entry_ref templateRef;
			while (templateDir.GetNextRef(&templateRef) == B_OK) {
				SettingsManager manager(ref.name, templateRef.name);
				BMessage settings = manager.Settings();
				BMessage tmplate = manager.Template();
			
				if (settings.IsEmpty() == true) continue;
				if (tmplate.IsEmpty() == true) continue;
				
				BString signature;
				bool autostart = false;

				if (tmplate.FindString("app_sig", &signature) != B_OK) continue;
				if (settings.FindBool("autostart", &autostart) != B_OK) continue;
				
				if (autostart) {
					status_t result = be_roster->Launch(signature.String());
					fprintf(stderr, "FeedServer::StartAutoStartClients(): Launching %s: %s\n",
						signature.String(), strerror(result));
				};
			};
		};
	} else {
		fprintf(stderr, "FeedServer::StartAutoStartClients(): Unable to determine settings dir");
	};
	
	fprintf(stderr, "FeedServer::StartAutoStartClients(): Done\n");
}

void FeedServer::StopAutoStartClients(void) {
	fprintf(stderr, "FeedServer::StopAutoStartClients(): Started\n");

	BPath settingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
		settingsPath.Append("BeClan/FeedKit/");
		
		BDirectory rootDir(settingsPath.Path());
		entry_ref ref;

		while (rootDir.GetNextRef(&ref) == B_OK) {
			BDirectory dir(&ref);
			if (dir.InitCheck() != B_OK) continue;
			if (dir.CountEntries() == 0) continue;
			if (dir.Contains("Templates", B_DIRECTORY_NODE) == false) continue;

			BDirectory templateDir(&dir, "Templates");
			if (templateDir.InitCheck() != B_OK) continue;
			if (templateDir.CountEntries() == 0) continue;
			
			entry_ref templateRef;
			while (templateDir.GetNextRef(&templateRef) == B_OK) {
				SettingsManager manager(ref.name, templateRef.name);
				BMessage settings = manager.Settings();
				BMessage tmplate = manager.Template();
			
				if (settings.IsEmpty() == true) continue;
				if (tmplate.IsEmpty() == true) continue;
				
				BString signature;
				bool autostart = false;

				if (tmplate.FindString("app_sig", &signature) != B_OK) continue;
				if (settings.FindBool("autostart", &autostart) != B_OK) continue;
				
				if (autostart) {
					BMessenger msgr(signature.String());
					msgr.SendMessage(B_QUIT_REQUESTED);
				};
			};
		};
	} else {
		fprintf(stderr, "FeedServer::StopAutoStartClients(): Unable to determine settings dir");
	};
	
	fprintf(stderr, "FeedServer::StopAutoStartClients(): Done\n");
}

Feed *FeedServer::MergeFeed(Feed *oldFeed, Feed *newFeed) {
	channel_cache_t oldChannel;
	channel_cache_t newChannel;
	channel_cache_t::iterator oIt;
	channel_cache_t::iterator nIt;
	Feed *merged = new Feed(newFeed->URL(), newFeed->Name());
	
	// Ensure all Feed elements are using current UUIDs
	UpdateFeedUUIDs(oldFeed);
	UpdateFeedUUIDs(newFeed);
	
	// Make a local Channel cache
	for (uint32 i = 0; i < oldFeed->ChannelCount(); i++) {
		Channel *channel = oldFeed->ChannelAt(i);
		oldChannel[channel->LocalUUID()] = channel;
	};

	for (uint32 i = 0; i < newFeed->ChannelCount(); i++) {
		Channel *channel = newFeed->ChannelAt(i);
		newChannel[channel->LocalUUID()] = channel;
	};
	
	// Loop through all the old Channels
	for (oIt = oldChannel.begin(); oIt != oldChannel.end(); oIt++) {
		nIt = newChannel.find(oIt->first);
		
		if (nIt == newChannel.end()) {
			// Archived Channel
			Channel *channel = new Channel(*oIt->second);
			for (uint32 i = 0; i < channel->ItemCount(); i++) {
				Item *item = channel->ItemAt(i);
				item->SetCurrent(false);
				item->SetNew(false);
			};

			merged->AddChannel(channel);
		} else {
			// Existing Channel - merge
			MergeChannel(merged, oIt->second, nIt->second);
		};
	};
	
	// Loop through all the new Channels
	for (nIt = newChannel.begin(); nIt != newChannel.end(); nIt++) {
		oIt = oldChannel.find(nIt->first);
		if (oIt == oldChannel.end()) {
			// New Channel - add
			Channel *channel = new Channel(*nIt->second);
			
			for (uint32 i = 0; i < channel->ItemCount(); i++) {
				Item *item = channel->ItemAt(i);
				item->SetNew(true);
				item->SetCurrent(true);
			};
			
			merged->AddChannel(channel);
		};
	};
	
	// Clear all previous Channels and Feeds
	for (uint32 i = 0; i < oldFeed->ChannelCount(); i++) {
		Channel *channel = oldFeed->ChannelAt(i);

		for (uint32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
			for (uint32 k = 0; k < item->EnclosureCount(); k++)
				fEnclosureUUID.erase(item->EnclosureAt(k)->UUID());
			fItemUUID.erase(item->UUID());
		};
		for (uint32 j = 0; j < channel->EnclosureCount(); j++)
			fEnclosureUUID.erase(channel->EnclosureAt(j)->UUID());
		if (channel->Image() != NULL)
			fEnclosureUUID.erase(channel->Image()->UUID());
		
		fChannelUUID.erase(channel->UUID());
	};
	
	UpdateFeedUUIDs(merged);
	for (reffeed_t::iterator rIt = fRefFeeds.begin();
		rIt != fRefFeeds.end(); rIt++) {
		if (rIt->second == oldFeed)
			rIt->second = merged;
	}
	delete oldFeed;
	delete newFeed;
	
	return merged;
};

Channel *FeedServer::MergeChannel(Feed *feed, Channel *oldChannel, Channel *newChannel) {
	item_cache_t oldItem;
	item_cache_t newItem;
	item_cache_t::iterator oIt;
	item_cache_t::iterator nIt;
	Channel *merged = new Channel(newChannel, false);
	feed->AddChannel(merged);
		
	// Make a local Item cache
	for (uint32 i = 0; i < oldChannel->ItemCount(); i++) {
		Item *item = oldChannel->ItemAt(i);
		oldItem[item->LocalUUID()] = item;
	};
	for (uint32 i = 0; i < newChannel->ItemCount(); i++) {
		Item *item = newChannel->ItemAt(i);
		newItem[item->LocalUUID()] = item;
	};
	
	// Loop through all old Items
	for (oIt = oldItem.begin(); oIt != oldItem.end(); oIt++) {
		Item *item = new Item(*oIt->second);
		nIt = newItem.find(oIt->first);
		
		if (nIt == newItem.end()) {
			// Archived item - set to non-current, non-new
			item->SetCurrent(false);
			item->SetNew(false);
		} else {
			// Existing item - set to non-new
			item->SetNew(false);
		};
		
		merged->AddItem(item);
	};

	// Loop through all new Items
	for (nIt = newItem.begin(); nIt != newItem.end(); nIt++) {
		oIt = oldItem.find(nIt->first);
		
		if (oIt == oldItem.end()) {
			// New Item - set to new and current
			Item *item = new Item(*nIt->second);
			item->SetCurrent(true);
			item->SetNew(true);

			merged->AddItem(item);
		};
	};
	
	return merged;
};

void FeedServer::UpdateFeedUUIDs(Feed *feed) {
	for (uint32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		
		for (uint32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
			
			if (item) item->UpdateUUID();
		};
		
		channel->UpdateUUID();
	};
	
	feed->UpdateUUID();
};

void FeedServer::UpdateReferences(Feed *feed) {
	fFeedUUID[feed->UUID()] = feed;
	
	for (uint32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		fChannelUUID[channel->UUID()] = channel;
		
		for (uint32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
			fItemUUID[item->UUID()] = item;
			
			for (uint32 k = 0; k < item->EnclosureCount(); k++) {
				Enclosure *enc = item->EnclosureAt(k);
				fEnclosureUUID[enc->UUID()] = enc;
			};
		};
		
		for (uint32 j = 0; j < channel->EnclosureCount(); j++) {
			Enclosure *enc = channel->EnclosureAt(j);
			fEnclosureUUID[enc->UUID()] = enc;
		};
		
		if (channel->Image()) fEnclosureUUID[channel->Image()->UUID()] = channel->Image();
	};
};

void FeedServer::FindExistingEnclosureDownloads(FeedKit::Feed *feed) {
	fprintf(stderr, "FeedServer::FindExistingEnclosureDownloads([%p] - %s)\n", feed, feed->DisplayName());

	BPath path;
	existing_enclosure_t::iterator eIt;
	int enclosures = 0;
	int downloaded = 0;
	int incomplete = 0;
	
	for (uint32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
	
		for (uint32 j = 0; j < channel->EnclosureCount(); j++) {
			Enclosure *enc = channel->EnclosureAt(j);
			EnclosureState state = None;

			eIt = fDownloadedEnclosure.find(enc->UUID());
			enclosures++;
			
			if (eIt != fDownloadedEnclosure.end()) {		
				LocalEnclosureDownload existing(eIt->second);

				state = Completed;
				if (existing.Complete() == false) {
					if (existing.Cancelled() == true) {
						state = Cancelled;
					} else {
						state = Queued;

						// Requeue the download - XXX Channel based Enclosure
//						DownloadEnclosure(feed, channel, item, enclosure, eIt->second,
//							existing.CurrentSize());
					};
					
					incomplete++;
				} else {
					downloaded++;
				};

				enc->SetLocalRef(eIt->second);
			};

			enc->SetState(state);
		};
	
		for (uint32 j = 0; j < channel->ItemCount(); j++) {
			Item *item = channel->ItemAt(j);
		
			for (uint32 k = 0; k < item->EnclosureCount(); k++) {
				Enclosure *enc = item->EnclosureAt(k);
				EnclosureState state = None;

				eIt = fDownloadedEnclosure.find(enc->UUID());
				enclosures++;
				
				if (eIt != fDownloadedEnclosure.end()) {
					LocalEnclosureDownload existing(eIt->second);
					
					state = Completed;
					if (existing.Complete() == false) {
						if (existing.Cancelled() == true) {
							state = Cancelled;
						} else {
							state = Queued;
	
							// Requeue the download
							DownloadEnclosure(feed, channel, item, enc, eIt->second, existing.CurrentSize());
						};		
						
						incomplete++;
					} else {
						downloaded++;
					};
				
					enc->SetLocalRef(eIt->second);
				};
				
				enc->SetState(state);
			};
		};
	};
	
	fprintf(stderr, "FeedServer::FindExistingEnclosureDownloads() - %i Enclosure(s). %i are complete, %i are still in progress\n", enclosures, downloaded, incomplete);
};

status_t FeedServer::DownloadEnclosure(FeedKit::Feed *feed, FeedKit::Channel *channel,
	FeedKit::Item *item, Enclosure *enclosure, entry_ref ref, int32 offset) {

	DelayedFlattenedObject progressUpdateCommand("feed", feed);
	BMessage progress(FeedKit::Private::FromServer::DownloadEnclosureProgress);
	progress.AddFlat("delayedupdate", &progressUpdateCommand);
	progress.AddString("channel", channel->UUID());
	progress.AddString("item", item->UUID());
	progress.AddString("enclosure", enclosure->UUID());

	FileRequest *request = new FileRequest(enclosure->URL(), ref, progress, offset);
	
	BMessage *start = new BMessage(kMsgEnclosureDownloadStart);
	start->AddString("url", enclosure->URL());
	start->AddPointer("filerequest", request);
	start->AddString("item", item->UUID());
	start->AddFlat("enclosure", enclosure);
	start->AddRef("ref", &ref);
	
	BMessage *complete = new BMessage(kMsgEnclosureDownloadFinish);
	complete->AddString("feed", feed->UUID());
	complete->AddString("channel", channel->UUID());
	complete->AddString("item", item->UUID());
	complete->AddString("enclosure", enclosure->UUID());
	complete->AddRef("ref", &ref);
	
	BMessage *error = new BMessage(kMsgEnclosureDownloadError);
	error->AddString("url", enclosure->URL());
	error->AddPointer("filerequest", request);
	error->AddPointer("item", item->UUID());
	error->AddString("enclosure", enclosure->UUID());
	error->AddRef("ref", &ref);
	
	BMessage *cancel = new BMessage(kMsgEnclosureDownloadCancelled);
	cancel->AddString("feed", feed->UUID());
	cancel->AddString("channel", channel->UUID());
	cancel->AddString("item", item->UUID());
	cancel->AddString("enclosure", enclosure->UUID());
	cancel->AddRef("ref", &ref);
	
	request->AddHandler(new EnclosureRequestHandler(this), enclosure);
	
	BMessenger *target = new BMessenger(this);
	if (fFeedDaemon->AddRequest(request, 0, target, start, complete, error,
		cancel) != B_OK) {
		DeleteUnqueuedRequest(request, target, start, complete, error, cancel);

//		XXX		
//		ErrorDetails error(ErrorCode::UnableToStartDownload, "Error starting the download");
//		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
//		reply.AddFlat("error", &error);		
//		msg->SendReply(&reply);
	
		return B_ERROR;
	};
	
	enclosure->SetState(Queued);
	
	return B_OK;
};

void FeedServer::CheckIndexes(void) {
	if (fSettingsPath.InitCheck() != B_OK) {
		fprintf(stderr, "FeedServer::CheckIndexes(): Settings path is unavailable\n");
		return;
	}

	dev_t device = dev_for_path(fSettingsPath.Path());
	if (device < B_OK) {
		fprintf(stderr, "FeedServer::CheckIndexes(): Unable to resolve settings volume: %s\n",
			strerror(device));
		return;
	}

	index_info info;
	if (fs_stat_index(device, kAttrEnclosureUUID, &info) == B_OK)
		return;

	int result = fs_create_index(device, kAttrEnclosureUUID, B_STRING_TYPE, 0);
	if (result != B_OK) {
		fprintf(stderr, "FeedServer::CheckIndexes(): Unable to create index \"%s\": %s\n",
			kAttrEnclosureUUID, strerror(errno));
		return;
	}

	fprintf(stderr, "FeedServer::CheckIndexes(): Created index \"%s\"\n",
		kAttrEnclosureUUID);
};

status_t FeedServer::CreateFeedStorage(Feed *feed) {
	if (feed == NULL || fSettingsPath.InitCheck() != B_OK)
		return B_BAD_VALUE;

	BPath path(fSettingsPath);
	if (!fIntegrationTest) {
		if (path.Append("BeClan") != B_OK || path.Append("FeedKit") != B_OK
			|| path.Append("Feed") != B_OK || path.Append("Data") != B_OK)
			return B_BAD_VALUE;
	}
	if (path.Append(feed->UUID()) != B_OK)
		return B_BAD_VALUE;

	BFile file(path.Path(), B_CREATE_FILE | B_READ_WRITE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	entry_ref ref;
	status = get_ref_for_path(path.Path(), &ref);
	if (status == B_OK)
		fRefFeedUUID[feed->UUID()] = ref;
	return status;
};

status_t FeedServer::SaveFeeds(void) {
	fprintf(stderr, "FeedServer::SaveFeeds(): Flattening %i feeds to disk\n", fRefFeedUUID.size());

	status_t result = B_OK;
	ref_uuid_t::iterator rIt;
	for (rIt = fRefFeedUUID.begin(); rIt != fRefFeedUUID.end(); rIt++) {
		feed_uuid_t::iterator uIt = fFeedUUID.find(rIt->first);
		
		if (uIt == fFeedUUID.end()) {
			fprintf(stderr, "FeedServer::SaveFeeds(): Could not find feed (%s) to save at %s\n", rIt->first.String(), rIt->second.name);
			if (result == B_OK)
				result = B_ENTRY_NOT_FOUND;
			continue;
		};

		BFile file(&rIt->second, B_READ_WRITE);
		status_t status = file.InitCheck();
		if (status == B_OK)
			status = file.SetSize(0);
		if (status != B_OK) {
			if (result == B_OK)
				result = status;
			continue;
		}

		Feed *feed = uIt->second;
		ssize_t length = feed->FlattenedSize();
		if (length <= 0) {
			if (result == B_OK)
				result = B_BAD_DATA;
			continue;
		}
		char *buffer = (char *)calloc(length, sizeof(char));
		if (buffer == NULL) {
			if (result == B_OK)
				result = B_NO_MEMORY;
			continue;
		}

		status = feed->Flatten(buffer, length);
		if (status == B_OK && file.Write(buffer, length) != length)
			status = B_IO_ERROR;
		free(buffer);
		if (status != B_OK && result == B_OK)
			result = status;
	};
	return result;
};

void FeedServer::RunIntegrationRestartTest(void) {
	BDirectory directory(fSettingsPath.Path());
	if (directory.InitCheck() != B_OK) {
		FailIntegrationTest("temporary persistence directory could not be reopened");
		return;
	}

	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		BEntry entry(&ref);
		if (!entry.IsDirectory() && LaunchFeed(ref, false) != B_OK) {
			FailIntegrationTest("a persisted feed could not be restored");
			return;
		}
	}

	Feed *rss = NULL;
	Feed *atom = NULL;
	urlfeed_t::iterator rssIt = fURLFeeds.find(fIntegrationRSSURL);
	urlfeed_t::iterator atomIt = fURLFeeds.find(fIntegrationAtomURL);
	if (rssIt != fURLFeeds.end())
		rss = rssIt->second;
	if (atomIt != fURLFeeds.end())
		atom = atomIt->second;

	bool valid = fURLFeeds.size() == 2 && fFeedUUID.size() == 2
		&& fRefFeedUUID.size() == 2 && rss != NULL && atom != NULL
		&& rss->ChannelCount() == 1 && atom->ChannelCount() == 1
		&& rss->ChannelAt(0)->ItemCount() == 1
		&& atom->ChannelAt(0)->ItemCount() == 1
		&& strcmp(rss->ChannelAt(0)->Title(),
			"FeedKit RSS integration fixture") == 0
		&& strcmp(atom->ChannelAt(0)->Title(),
			"FeedKit Atom integration fixture") == 0
		&& strcmp(rss->ChannelAt(0)->ItemAt(0)->Title(),
			"RSS fixture item") == 0
		&& strcmp(atom->ChannelAt(0)->ItemAt(0)->Title(),
			"Atom fixture item") == 0;
	for (ref_uuid_t::iterator it = fRefFeedUUID.begin();
		valid && it != fRefFeedUUID.end(); it++) {
		if (strcmp(it->first.String(), it->second.name) != 0)
			valid = false;
	}

	if (!valid) {
		FailIntegrationTest("persisted feeds changed across restart");
		return;
	}

	fIntegrationTestComplete = true;
	delete fIntegrationTimeoutRunner;
	fIntegrationTimeoutRunner = NULL;
	fprintf(stderr, "FeedServer integration restart test passed: two feeds restored with stable UUIDs and content\n");
	PostMessage(B_QUIT_REQUESTED);
};

void FeedServer::FailIntegrationTest(const char *reason) {
	if (!fIntegrationTest || fIntegrationTestComplete)
		return;

	fIntegrationTestFailed = true;
	fIntegrationTestComplete = true;
	fprintf(stderr, "FeedServer integration test failed: %s\n", reason);
	PostMessage(B_QUIT_REQUESTED);
};

void FeedServer::RecordIntegrationDownload(Feed *feed) {
	if (!fIntegrationTest || fIntegrationTestComplete)
		return;

	const char *expectedTitle = NULL;
	const char *expectedItemTitle = NULL;
	if (strcmp(feed->URL(), fIntegrationRSSURL.String()) == 0) {
		expectedTitle = "FeedKit RSS integration fixture";
		expectedItemTitle = "RSS fixture item";
	} else if (strcmp(feed->URL(), fIntegrationAtomURL.String()) == 0) {
		expectedTitle = "FeedKit Atom integration fixture";
		expectedItemTitle = "Atom fixture item";
	} else {
		FailIntegrationTest("a parser returned an unexpected feed URL");
		return;
	}

	if (feed->ChannelCount() != 1) {
		FailIntegrationTest("a fixture did not produce exactly one channel");
		return;
	}

	Channel *channel = feed->ChannelAt(0);
	if (channel == NULL || strcmp(channel->Title(), expectedTitle) != 0) {
		FailIntegrationTest("a fixture channel title was parsed incorrectly");
		return;
	}
	if (channel->ItemCount() != 1 || channel->ItemAt(0) == NULL
		|| strcmp(channel->ItemAt(0)->Title(), expectedItemTitle) != 0) {
		FailIntegrationTest("a fixture item count or title was parsed incorrectly");
		return;
	}

	int32 count = ++fIntegrationDownloads[feed->URL()];
	fprintf(stderr, "FeedServer integration test: %s download %i validated\n",
		expectedTitle, count);

	int32 rssCount = fIntegrationDownloads[fIntegrationRSSURL];
	int32 atomCount = fIntegrationDownloads[fIntegrationAtomURL];
	if (rssCount == 1 && atomCount == 1) {
		fprintf(stderr, "FeedServer integration test: forcing fixture refreshes\n");
		BMessage refreshRSS(FeedKit::Private::ToServer::ForceRefresh);
		refreshRSS.AddString("url", fIntegrationRSSURL.String());
		PostMessage(&refreshRSS);

		BMessage refreshAtom(FeedKit::Private::ToServer::ForceRefresh);
		refreshAtom.AddString("url", fIntegrationAtomURL.String());
		PostMessage(&refreshAtom);
	} else if (rssCount >= 2 && atomCount >= 2
		&& !fIntegrationFailureRequested) {
		if (fIntegrationFailureURL.Length() > 0) {
			fIntegrationFailureRequested = true;
			fprintf(stderr, "FeedServer integration test: requesting expected missing feed\n");
			BMessage missing(FeedKit::Private::ToServer::RegisterFeed);
			missing.AddString("url", fIntegrationFailureURL.String());
			missing.AddString("name", "FeedKit missing integration fixture");
			PostMessage(&missing);
		} else {
			fIntegrationTestComplete = true;
			delete fIntegrationTimeoutRunner;
			fIntegrationTimeoutRunner = NULL;
			fprintf(stderr, "FeedServer integration test passed: RSS and Atom registered and refreshed\n");
			PostMessage(B_QUIT_REQUESTED);
		}
	}
};

//#pragma mark Private Event Handlers

status_t FeedServer::HandleCheckDownloads(BMessage */*msg*/) {
	request_t active = fFeedDaemon->ActiveRequests();
	int32 requests = active.size();
	
	if (requests == 0) return B_OK;
	
	for (int32 i = 0; i < requests; i++) {
		FileRequest *request = active[i];
	
		BMessage progressMsg(request->ProgressMessage());
		if (progressMsg.IsEmpty() == false) {			
			DownloadProgress *progress = ProgressFromRequest(request);
			progressMsg.AddFlat("download", progress);
			
			Broadcast(&progressMsg);
		};
	};
	
	return B_OK;
};

status_t FeedServer::HandleChannelQuery(BMessage *msg) {
	int32 querywhat = B_ERROR;
	if (msg->FindInt32("query_what", &querywhat) != B_OK) return B_ERROR;
	
	switch (querywhat) {
		case QL::Notifications::InitialFetch: {
			int32 entries = fFeedQuery->CountEntries();
			for (int32 i = 0; i < entries; i++) {
				entry_ref ref = fFeedQuery->EntryAt(i);
				LaunchFeed(ref);
				// XXX
				break;
			};
		} break;
		
		case QL::Notifications::EntryAdded: {
			entry_ref ref;
			if (msg->FindRef("affected_ref", &ref) != B_OK) return B_ERROR;
			LaunchFeed(ref);
		} break;
		
		case QL::Notifications::EntryRemoved: {
		} break;
	};

	return B_OK;
};

status_t FeedServer::HandleEnclosureQuery(BMessage *msg) {
	int32 querywhat = B_ERROR;
	if (msg->FindInt32("query_what", &querywhat) != B_OK) return B_ERROR;

	switch (querywhat) {
		case QL::Notifications::InitialFetch: {
			int32 entries = fEnclosureQuery->CountEntries();
	
			for (int32 i = 0; i < entries; i++) {	
				entry_ref ref = fEnclosureQuery->EntryAt(i);
				BNode node(&ref);
				
				if (node.InitCheck() == B_OK) {
					BPath path(&ref);
					int32 length = B_ERROR;
					char *uuid = ReadAttribute(node, kAttrEnclosureUUID, &length);
					
					fprintf(stderr, "FeedServer::HandleEnclosureQuery() - Initial Fetch %s\n", path.Path());

					if (uuid != NULL) {
						fDownloadedEnclosure[BString(uuid, length)] = ref;						

						free(uuid);
					};
				};
			};
		} break;
		
		case QL::Notifications::EntryAdded: {
			entry_ref ref;
			
			for (int32 i = 0; msg->FindRef("affected_ref", i, &ref) == B_OK; i++) {
				BNode node(&ref);			
				if (node.InitCheck() == B_OK) {
					int32 length = B_ERROR;
					char *uuid = ReadAttribute(node, kAttrEnclosureSize, &length);
					BPath path(&ref);
					
					fprintf(stderr, "FeedServer::HandleEnclosureQuery() - Entry added %s\n", path.Path());

					if (uuid != NULL) {
						fDownloadedEnclosure[BString(uuid, length)] = ref;
						
						free(uuid);
					};
				};						
			};
		} break;
		
		case QL::Notifications::EntryRemoved: {
			entry_ref ref;
			
			for (int32 i = 0; msg->FindRef("affected_ref", i, &ref) == B_OK; i++) {
				for (existing_enclosure_t::iterator it = fDownloadedEnclosure.begin(); it != fDownloadedEnclosure.end(); it++) {
					if (it->second == ref) {
						fDownloadedEnclosure.erase(it);
						break;
					};
				};
			};
		} break;
	};
	
	return B_OK;
};

status_t FeedServer::HandleFeedDownloadStart(BMessage *msg) {
	FileRequest *request = NULL;
	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;

	urlfeed_t::iterator uIt = fURLFeeds.find(request->URL());
	if (uIt == fURLFeeds.end()) {
		// Initial registration has no Feed object to attach to the progress
		// notification yet. The download itself still started successfully.
		return B_OK;
	};

	DownloadProgress *progress = ProgressFromRequest(request);		

	BMessage startNotify(FeedKit::Private::FromServer::DownloadFeedStarted);
	startNotify.AddFlat("download", progress);
	startNotify.AddFlat("feed", uIt->second);
	
	Broadcast(&startNotify);
	delete progress;

	return B_OK;
};

status_t FeedServer::HandleFeedDownloadError(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	FileRequest *request = NULL;
	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;
	bool expectedFailure = fIntegrationTest && fIntegrationFailureRequested
		&& strcmp(request->URL(), fIntegrationFailureURL.String()) == 0
		&& request->UsesTemporaryStorage()
		&& request->StorageStatus() == B_OK;
	status_t releaseStatus = request->ReleaseTemporaryStorage();

	urlfeed_t::iterator uIt = fURLFeeds.find(request->URL());
	if (uIt == fURLFeeds.end()) {
		const char *name = "";
		msg->FindString("name", &name);
		Feed feed(request->URL(), name);
		BString description("Unable to download feed: ");
		description << request->Error();
		if (request->HTTPStatus() > 0)
			description << " (HTTP " << request->HTTPStatus() << ")";
		ErrorDetails error(ErrorCode::UnableToDownloadFeed,
			description.String());
		BMessage errorNotify(FeedKit::Private::FromServer::RegisterFeedError);
		errorNotify.AddFlat("error", &error);
		errorNotify.AddFlat("feed", &feed);
		Broadcast(&errorNotify);
	} else {
		DownloadProgress *progress = ProgressFromRequest(request);
		BMessage errorNotify(FeedKit::Private::FromServer::DownloadFeedError);
		errorNotify.AddFlat("download", progress);
		errorNotify.AddFlat("feed", uIt->second);
		Broadcast(&errorNotify);
		delete progress;
	}

	if (fIntegrationTest) {
		if (expectedFailure && releaseStatus == B_OK
			&& !request->UsesTemporaryStorage()) {
			fprintf(stderr, "FeedServer integration test: expected download failure validated\n");
			if (fIntegrationParseFailureURL.Length() > 0) {
				fIntegrationParseFailureRequested = true;
				fprintf(stderr, "FeedServer integration test: requesting expected parse failure\n");
				BMessage malformed(FeedKit::Private::ToServer::RegisterFeed);
				malformed.AddString("url", fIntegrationParseFailureURL.String());
				malformed.AddString("name", "FeedKit malformed integration fixture");
				PostMessage(&malformed);
			} else {
				fIntegrationTestComplete = true;
				delete fIntegrationTimeoutRunner;
				fIntegrationTimeoutRunner = NULL;
				fprintf(stderr, "FeedServer integration test passed: RSS and Atom registered and refreshed\n");
				PostMessage(B_QUIT_REQUESTED);
			}
			return B_OK;
		}
		FailIntegrationTest("a fixture download failed unexpectedly");
		return B_ERROR;
	}

	return releaseStatus;
};

status_t FeedServer::HandleFeedDownloadFinish(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	const char *url = NULL;
	const char *name = NULL;
	FileRequest *request = NULL;

	if (msg->FindString("url", &url) != B_OK) return B_ERROR;
	if (msg->FindString("name", &name) != B_OK) name = "";
	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;
	
	DownloadProgress *progress = ProgressFromRequest(request);

	if (request->Size() == 0) {
		fprintf(stderr, "File not downloaded successfully\n");
		request->ReleaseTemporaryStorage();
		if (fIntegrationTest)
			FailIntegrationTest("a fixture download was empty");
		
		BMessage errorNotify(FeedKit::Private::FromServer::DownloadFeedError);
		errorNotify.AddFlat("download", progress);
		Broadcast(&errorNotify);
		delete progress;
		
		return B_ERROR;
	};
		
	Feed notifyFeed;
	urlfeed_t::iterator uIt = fURLFeeds.find(request->URL());
	if (uIt == fURLFeeds.end()) {
		notifyFeed = Feed(url, name);
	} else {
		notifyFeed = *(uIt->second);
	};

	BMessage finishNotify(FeedKit::Private::FromServer::DownloadFeedFinished);
	finishNotify.AddFlat("download", progress);
	finishNotify.AddFlat("feed", &notifyFeed);
	Broadcast(&finishNotify);
	delete progress;

	char *buffer = (char *)calloc(request->Size(), sizeof(char));
	ssize_t bytesRead = buffer == NULL ? B_NO_MEMORY
		: request->Read(buffer, request->Size());
	status_t releaseStatus = request->ReleaseTemporaryStorage();
	if (bytesRead != request->Size() || releaseStatus != B_OK) {
		free(buffer);
		if (fIntegrationTest)
			FailIntegrationTest("a downloaded fixture could not be read");
		return B_IO_ERROR;
	}
				
	Feed *refreshed = fParserManager->Parse(url, buffer, request->Size());
	free(buffer);
	
	if (refreshed == NULL) {
		bool expectedParseFailure = fIntegrationTest
			&& fIntegrationParseFailureRequested
			&& strcmp(url, fIntegrationParseFailureURL.String()) == 0;

		BMessage errorMsg(FeedKit::Private::FromServer::RegisterFeedError);

		ErrorDetails error(ErrorCode::UnableToParseFeed, "Unable to parse feed");
		Feed feed(url, name);
					
		errorMsg.AddFlat("error", &error);
		errorMsg.AddFlat("feed", &feed);

		Broadcast(&errorMsg);

		if (expectedParseFailure) {
			bool validFeedsIntact = fURLFeeds.size() == 2
				&& fFeedUUID.size() == 2
				&& fURLFeeds.find(fIntegrationRSSURL) != fURLFeeds.end()
				&& fURLFeeds.find(fIntegrationAtomURL) != fURLFeeds.end()
				&& fURLFeeds.find(fIntegrationParseFailureURL) == fURLFeeds.end();
			if (!validFeedsIntact) {
				FailIntegrationTest("a malformed feed altered registered feed state");
				return B_ERROR;
			}
			if (fIntegrationPersistence && SaveFeeds() != B_OK) {
				FailIntegrationTest("validated feeds could not be persisted");
				return B_ERROR;
			}
			fIntegrationTestComplete = true;
			delete fIntegrationTimeoutRunner;
			fIntegrationTimeoutRunner = NULL;
			fprintf(stderr, "FeedServer integration test: expected parse failure validated without changing registered feeds\n");
			fprintf(stderr, "FeedServer integration test passed: RSS and Atom registered and refreshed; failures isolated\n");
			PostMessage(B_QUIT_REQUESTED);
			return B_OK;
		}
		if (fIntegrationTest)
			FailIntegrationTest("a fixture could not be parsed");
		
		return B_ERROR;
	};
	
	MarkItems(refreshed, true, true);
	
	bool wasNewFeed = false;
	const char *feedUUID = refreshed->UUID();
	feed_uuid_t::iterator feIt = fFeedUUID.find(feedUUID);

	Feed *feed = NULL;
	if (feIt != fFeedUUID.end()) {
		feed = MergeFeed(feIt->second, refreshed);
	} else {
		feed = refreshed;
		wasNewFeed = true;
	};

	feed->Name(name);
	UpdateReferences(feed);
	FindExistingEnclosureDownloads(feed);
	
	fFeedUUID[feed->UUID()] = feed;
	
	for (uint32 i = 0; i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		fChannelUUID[channel->UUID()] = channel;
		
		for (uint32 j = 0; j < channel->ItemCount(); j++) {		
			Item *item = channel->ItemAt(j);
			
			if (item) fItemUUID[item->UUID()] = item;
		};
	};
	
	bigtime_t interval = (bigtime_t)1E18;
	uint32 channels = feed->ChannelCount();

	for (uint32 i = 0; i < channels; i++) {
		Channel *channel = feed->ChannelAt(i);
		
		// Get the lowest update interval of all channels on the Feed
		if (channel->TTL() > 0) {
			interval = (bigtime_t)min_c(interval, (double)channel->TTL() * (double)kMinute);
		};
	};
	
	fURLFeeds[feed->URL()] = feed;

	BMessage refreshMsg(FeedKit::Private::ToServer::ForceRefresh);
	refreshMsg.AddString("url", url);
				
	BMessageRunner *refresh = new BMessageRunner(BMessenger(this), &refreshMsg,	interval);

	// If there's an old refresh runner, delete it
	refresh_t::iterator rIt = fRefreshRunners.find(url);
	if (rIt != fRefreshRunners.end()) delete rIt->second;

	fRefreshRunners[url] = refresh; 
//	delete request;
// XXX 
	
	// Look for any Images on this feed's channels. Integration fixtures stay
	// entirely in memory and must not create user data.
	for (uint32 i = 0; !fIntegrationTest && i < feed->ChannelCount(); i++) {
		Channel *channel = feed->ChannelAt(i);
		Enclosure *image = channel->Image();
		if (image) {
			BPath path(fSettingsPath);
			path.Append("BeClan");
			path.Append("FeedKit");
			path.Append("Feed");
			path.Append("Data");
			path.Append("ChannelIcons");
			
			path.Append(channel->UUID());
			
			BString imgPath(path.Path());

			entry_ref imgRef;
			get_ref_for_path(imgPath.String(), &imgRef);
			
			FileRequest *imgRequest = new FileRequest(image->URL(), imgRef, BMessage());
		
			BMessage *start = new BMessage(kMsgChannelIconDownloadStart);
			start->AddString("url", image->URL());
			start->AddString("name", image->Description());
			start->AddPointer("filerequest", imgRequest);
			
			BMessage *complete = new BMessage(kMsgChannelIconDownloadFinish);
			complete->AddString("url", image->URL());
			complete->AddString("name", image->Description());
			complete->AddPointer("filerequest", imgRequest);
			complete->AddPointer("channel", channel);
			
			BMessage *error = new BMessage(kMsgChannelIconDownloadError);
			error->AddString("url", image->URL());
			error->AddString("name", image->Description());
			error->AddPointer("filerequest", imgRequest);
			
			BMessenger *target = new BMessenger(this);
			if (fFeedDaemon->AddRequest(imgRequest, 0, target, start,
				complete, error) != B_OK) {
				DeleteUnqueuedRequest(imgRequest, target, start, complete,
					error);
			}
		};
	};
	
	if (wasNewFeed) {
		if (!fIntegrationTest || fIntegrationPersistence) {
			status_t storageStatus = CreateFeedStorage(feed);
			if (storageStatus != B_OK) {
				if (fIntegrationTest)
					FailIntegrationTest("a fixture persistence file could not be created");
				return storageStatus;
			}
		}

		// New feed, send a "Subscribe Complete"
		BMessage subscribed(FeedKit::Private::FromServer::RegisterFeedComplete);
		subscribed.AddFlat("feed", feed);		
		Broadcast(&subscribed);
	} else {
		for (uint32 i = 0; i < feed->ChannelCount(); i++) {
			Channel *channel = feed->ChannelAt(i);
			ItemList newItems = channel->FindItems(new NewItemSpecification(), true);
			
			if (newItems.size() > 0) {
				BMessage updated(FeedKit::Private::FromServer::ChannelUpdated);
				updated.AddFlat("feed", feed);
				updated.AddString("channel", channel->UUID());
			
				Broadcast(&updated);
			}
		};
	};

	RecordIntegrationDownload(feed);
	
	return B_OK;
};

status_t FeedServer::HandleForceRefresh(BMessage *msg) {
	const char *url = NULL;
	
	if (msg->FindString("url", &url) == B_OK) {
		SyndicateURL(url);
	} else {
		urlfeed_t::iterator uIt;

		for (uIt = fURLFeeds.begin(); uIt != fURLFeeds.end(); uIt++) {
			SyndicateURL(uIt->first.String());
		};
	};
	
	return B_OK;
};

status_t FeedServer::HandleMarkRead(BMessage *msg) {
	const char *uuid = NULL;
	if (msg->FindString("item", &uuid) != B_OK) return B_ERROR;
	
	item_uuid_t::iterator iIt = fItemUUID.find(uuid);
	if (iIt != fItemUUID.end()) {
		Item *item = iIt->second;
		Channel *channel = item->ParentChannel();
		Feed *feed = channel->ParentFeed();
		
		item->SetRead(true);
		
		BMessage itemRead(FeedKit::Private::FromServer::ItemRead);
		itemRead.AddFlat("feed", feed);
		itemRead.AddString("channel", channel->UUID());
		itemRead.AddString("item", item->UUID());
		
		Broadcast(&itemRead);
	};

	return B_OK;
};

status_t FeedServer::HandleDownloadEnclosure(BMessage *msg) {
	const char *itemUUID = NULL;
	Item *item = NULL;
	Enclosure enc;
	const char *path = NULL;
	
	if (msg->FindString("item", &itemUUID) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidItem, "No Item was specified");
		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
	
		return B_ERROR;
	};
	
	if (msg->FindFlat("enclosure", &enc) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidEnclosure, "No Enclosure was specified");
		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
	
		return B_ERROR;
	};
	
	item_uuid_t::iterator iIt = fItemUUID.find(itemUUID);
	if (iIt == fItemUUID.end()) {
		ErrorDetails error(ErrorCode::InvalidItem, "Could not find specified Item");
		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
		reply.AddFlat("error", &error);
	
		msg->SendReply(&reply);

		return B_ERROR;
	};
	
	item = iIt->second;
	Channel *channel = item->ParentChannel();
	Feed *feed = channel->ParentFeed();
				
	if (msg->FindString("path", &path) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidEnclosurePath, "No path specified");
		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
		
		return B_ERROR;
	};
	
	BFile file(path, B_CREATE_FILE | B_READ_WRITE);
	status_t result = file.InitCheck();
	if (result != B_OK) {
		BString desc = "Error opening save path: ";
		desc << strerror(result);
		
		ErrorDetails error(ErrorCode::InvalidEnclosurePath, desc.String());
		BMessage reply(FeedKit::Private::FromServer::DownloadEnclosureError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
		
		return B_ERROR;
	};
	
	entry_ref ref;
	get_ref_for_path(path, &ref);
	int32 offset = 0;
	
	// Check if the Enclosure has already been downloaded
	LocalEnclosureDownload existing;
	existing_enclosure_t::iterator dIt = fDownloadedEnclosure.find(enc.UUID());
	fprintf(stderr, "HandleDownloadEnclosure: Enclosure (%s) was %sfound - %i existing\n", enc.UUID(),
		dIt != fDownloadedEnclosure.end() ? "" : "not ", fDownloadedEnclosure.size());
	
	if (dIt != fDownloadedEnclosure.end()) {
		existing = LocalEnclosureDownload(ref);

		if (existing.Complete() == true) {
			BMessage complete(FeedKit::Private::FromServer::DownloadEnclosureFinished);
			complete.AddFlat("feed", feed);
			complete.AddString("channel", channel->UUID());
			complete.AddString("item", item->UUID());
			complete.AddString("enclosure", enc.UUID());
			complete.AddRef("ref", &ref);
		
			Broadcast(&complete);
		} else {
			offset = existing.CurrentSize();
		};
	} else {
		existing = LocalEnclosureDownload(ref, &enc);
	};
	
	return DownloadEnclosure(feed, channel, item, &enc, ref, offset);
};

status_t FeedServer::HandleCancelEnclosureDownload(BMessage *msg) {
	fprintf(stderr, "FeedServer::HandleCancelEnclosureDownload() called\n");
	
	const char *itemUUID = NULL;
	Enclosure enc;
	
	if (msg->FindString("item", &itemUUID) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidItem, "No Item was specified");
		BMessage reply(FeedKit::Private::FromServer::CancelEnclosureDownloadError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
	
		return B_ERROR;
	};
	
	if (msg->FindFlat("enclosure", &enc) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidEnclosure, "No Enclosure was specified");
		BMessage reply(FeedKit::Private::FromServer::CancelEnclosureDownloadError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
	
		return B_ERROR;
	};
	
	item_uuid_t::iterator iIt = fItemUUID.find(itemUUID);
	if (iIt == fItemUUID.end()) {
		ErrorDetails error(ErrorCode::InvalidItem, "Could not find specified Item");
		BMessage reply(FeedKit::Private::FromServer::CancelEnclosureDownloadError);
		reply.AddFlat("error", &error);
	
		msg->SendReply(&reply);

		return B_ERROR;
	};

	if (fFeedDaemon->CancelDownload(enc.URL()) != B_OK) {
		ErrorDetails error(ErrorCode::InvalidEnclosure, "Could not find Enclosure to download");
		BMessage reply(FeedKit::Private::FromServer::CancelEnclosureDownloadError);
		reply.AddFlat("error", &error);
		
		msg->SendReply(&reply);
		
		return B_ERROR;
	};
	
	enc.SetState(Cancelled);
	
	// Update the on-disk attribute for this enclosure
	existing_enclosure_t::iterator eIt = fDownloadedEnclosure.find(enc.UUID());
	LocalEnclosureDownload existing;
	if (eIt != fDownloadedEnclosure.end()) {
		existing = LocalEnclosureDownload(eIt->second);
	} else {
		existing = LocalEnclosureDownload(enc.LocalRef(), &enc);
		fDownloadedEnclosure[enc.UUID()] = enc.LocalRef();
	}
	
	existing.CancelDownload();
	
	return B_OK;
};

status_t FeedServer::HandleEnclosureDownloadStart(BMessage *msg) {
	const char *itemUUID = NULL;
	FileRequest *request = NULL;
	entry_ref ref;
	Enclosure enclosure;

	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;
	if (msg->FindString("item", &itemUUID) != B_OK) return B_ERROR;
	if (msg->FindFlat("enclosure", &enclosure) != B_OK) return B_ERROR;
	if (msg->FindRef("ref", &ref) != B_OK) return B_ERROR;

	int32 size = request->ExpectedSize();
	if (size == 0) size = request->Size();

	item_uuid_t::iterator iIt = fItemUUID.find(itemUUID);
	if (iIt == fItemUUID.end()) return B_ERROR;

	Item *item = iIt->second;
	Channel *channel = item->ParentChannel();
	Feed *feed = channel->ParentFeed();
	DownloadProgress *progress = ProgressFromRequest(request);

	enclosure.SetState(Downloading);

	BMessage startNotify(FeedKit::Private::FromServer::DownloadEnclosureStarted);
	startNotify.AddFlat("feed", feed);
	startNotify.AddString("channel", channel->UUID());
	startNotify.AddString("item", item->UUID());
	startNotify.AddString("enclosure", enclosure.UUID());
	
	Broadcast(&startNotify);

	return B_OK;
};

status_t FeedServer::HandleEnclosureDownloadFinish(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	const char *feedUUID = NULL;
	const char *chanUUID = NULL;
	const char *itemUUID = NULL;
	const char *encUUID = NULL;
	entry_ref ref;
	Feed *feed = NULL;
	Enclosure *enc = NULL;

	if (msg->FindString("feed", &feedUUID) != B_OK) return B_ERROR;
	if (msg->FindString("channel", &chanUUID) != B_OK) return B_ERROR;
	if (msg->FindString("item", &itemUUID) != B_OK) return B_ERROR;
	if (msg->FindString("enclosure", &encUUID) != B_OK) return B_ERROR;
	if (msg->FindRef("ref", &ref) != B_OK) return B_ERROR;

	feed = GetFeed(feedUUID);
	enc = GetEnclosure(encUUID);

	if (feed == NULL || enc == NULL) return B_NAME_NOT_FOUND;

	enc->SetLocalRef(ref);
	enc->SetState(Completed);

	int32 len = strlen(enc->MIME());
	if (len > 0) {
		BNode node(&ref);
		node.WriteAttr("BEOS:TYPE", B_MIME_TYPE, 0, enc->MIME(), len);
		node.Unset();
	};

	BMessage notify(FeedKit::Private::FromServer::DownloadEnclosureFinished);
	notify.AddFlat("feed", feed);
	notify.AddString("channel", chanUUID);
	notify.AddString("item", itemUUID);
	notify.AddString("enclosure", encUUID);
	
	Broadcast(&notify);

	return B_OK;
};

status_t FeedServer::HandleEnclosureDownloadError(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	printf("Enclosure download error\n");
	msg->PrintToStream();

	return B_ERROR;
};

status_t FeedServer::HandleEnclosureDownloadCancelled(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	const char *feedUUID = NULL;
	const char *chanUUID = NULL;
	const char *itemUUID = NULL;
	const char *encUUID = NULL;
	entry_ref ref;
	Feed *feed = NULL;
	Enclosure *enc = NULL;

printf("HandleEnclosureDownloadCancelled\n");
msg->PrintToStream();

	if (msg->FindString("feed", &feedUUID) != B_OK) return B_ERROR;
	if (msg->FindString("channel", &chanUUID) != B_OK) return B_ERROR;
	if (msg->FindString("item", &itemUUID) != B_OK) return B_ERROR;
	if (msg->FindString("enclosure", &encUUID) != B_OK) return B_ERROR;
	if (msg->FindRef("ref", &ref) != B_OK) return B_ERROR;

	feed = GetFeed(feedUUID);
	enc = GetEnclosure(encUUID);

	if (feed == NULL || enc == NULL) return B_NAME_NOT_FOUND;

	enc->SetState(Cancelled);

	BMessage notify(FeedKit::Private::FromServer::EnclosureDownloadStatusChanged);
	notify.AddFlat("feed", feed);
	notify.AddString("channel", chanUUID);
	notify.AddString("item", itemUUID);
	notify.AddString("enclosure", encUUID);
	notify.AddRef("ref", &ref);
	
	Broadcast(&notify);

	return B_OK;
};

status_t FeedServer::HandleRegisterFeed(BMessage *msg) {
	const char *url = NULL;
	const char *name = NULL;
	if (msg->FindString("url", &url) != B_OK) return B_ERROR;
	if (msg->FindString("name", &name) != B_OK) name = "";

	SyndicateURL(url, name);

	return B_OK;
};

status_t FeedServer::HandleGetFeedList(BMessage *msg) {
	BMessage feeds(FeedKit::Private::ServerReply::FeedList);
	
	feed_uuid_t::iterator uIt;
	for (uIt = fFeedUUID.begin(); uIt != fFeedUUID.end(); uIt++) {
		feeds.AddFlat("feed", uIt->second);
	};
			
	msg->SendReply(&feeds);

	return B_OK;
};

status_t FeedServer::HandleGetChannelIconPath(BMessage *msg) {
	BMessage reply(B_REPLY);
	const char *uuid = NULL;
	
	if (msg->FindString("channel", &uuid) != B_OK) {
		reply.what = FeedKit::Private::ServerReply::Error;
		reply.AddString("description", "No Channel was provided");
	} else {
		channel_icon_t::iterator cIt = fChannelIcon.find(uuid);
		
		if (cIt == fChannelIcon.end()) {
			reply.what = FeedKit::Private::ServerReply::Error;
			reply.AddString("description", "Channel icon has not been downloaded yet");					
		} else {
			reply.what = FeedKit::Private::FromServer::ChannelIconPath;
			reply.AddString("path", cIt->second);
		};
	};
	
	msg->SendReply(&reply);

	return B_OK;
};

status_t FeedServer::HandleAddListener(BMessage *msg) {
	BMessenger msgr;
	if (msg->FindMessenger("listener", &msgr) != B_OK) return B_ERROR;
	
	AddListener(msgr);
	
	msg->SendReply(FeedKit::Private::Success);

	return B_OK;
};

status_t FeedServer::HandleRemoveListener(BMessage *msg) {
	BMessenger msgr;
	if (msg->FindMessenger("listener", &msgr) != B_OK) return B_ERROR;
	
	RemoveListener(msgr);
	
	msg->SendReply(FeedKit::Private::Success);

	return B_OK;
};

status_t FeedServer::HandleSettingsTemplateUpdated(BMessage */*msg*/) {
	return B_ERROR;
};

status_t FeedServer::HandleNodeMonitor(BMessage *msg) {
	int32 opcode = B_ERROR;
	if (msg->FindInt32("opcode", &opcode) != B_OK) return B_ERROR;
	
	switch (opcode) {
		case B_DEVICE_MOUNTED: {
			printf("Oh noes! Volume mounted\n");
		} break;
		
		case B_DEVICE_UNMOUNTED: {
			printf("Oh noes! Volume unmounted\n");
		} break;

		default: {
			printf("Unhandled node monitor message: %4.4s\n", (char *)&opcode);
		};
	};

	return B_OK;
};

status_t FeedServer::HandleChannelIconDownloadStart(BMessage *msg) {
	FileRequest *request = NULL;
	Channel *channel = NULL;

	(void)channel;
	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;

	fprintf(stderr, "FeedServer:: Size %i vs Expected Size: %i\n", request->Size(), request->ExpectedSize());

	return B_OK;
};

status_t FeedServer::HandleChannelIconDownloadFinish(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	FileRequest *request = NULL;
	Channel *channel = NULL;

	if (msg->FindPointer("filerequest", reinterpret_cast<void **>(&request)) != B_OK) return B_ERROR;
	if (msg->FindPointer("channel", reinterpret_cast<void **>(&channel)) != B_OK) return B_ERROR;
	
	entry_ref ref = request->Ref();
	BMessage iconUpdated(FeedKit::Private::FromServer::ChannelIconUpdated);
	iconUpdated.AddFlat("feed", channel->ParentFeed());
	iconUpdated.AddString("channel", channel->UUID());
	iconUpdated.AddRef("ref", &ref);

	Broadcast(&iconUpdated);

	return B_OK;
};

// XXX Handle
status_t FeedServer::HandleChannelIconDownloadError(BMessage *msg) {
	DownloadTerminalReply terminalReply(msg);
	return B_OK;
};

status_t FeedServer::HandleChangeFeedSubscription(BMessage *msg) {
	const char *uuid = NULL;
	Feed *feed = NULL;
	bool subscribed = false;
	
	if (msg->FindString("uuid", &uuid) != B_OK) {
		BMessage reply(B_REPLY);
		reply.what = FeedKit::Private::ServerReply::Error;
		reply.AddString("description", "No Feed was provided");

		msg->SendReply(&reply);
		
		return B_OK;
	};
	if (msg->FindBool("subscribed", &subscribed) != B_OK) return B_OK;
	
	feed = GetFeed(uuid);
	if (feed == NULL) {
		BMessage reply(B_REPLY);
		reply.what = FeedKit::Private::ServerReply::Error;
		reply.AddString("description", "Feed could not be found");

		msg->SendReply(&reply);
		
		return B_OK;
	};

	feed->SetSubscribed(subscribed);
	
	fprintf(stderr, "FeedServer::HandleChangeFeedSubscription: %s is now %ssubscribed\n", feed->DisplayName(), subscribed ? "" : "un");
	
	BMessage feedSubscribed(FeedKit::Private::FromServer::FeedSubscriptionChanged);
	feedSubscribed.AddFlat("feed", feed);
	Broadcast(&feedSubscribed);
	
	if (subscribed == false) {
		// Delete the refresh runner so we stop updating the feed
		refresh_t::iterator rIt = fRefreshRunners.find(feed->URL());
		if (rIt != fRefreshRunners.end()) {
			delete rIt->second;
			fRefreshRunners.erase(rIt);
		};
	} else {
		// Need to subscribe the the Feed
		SyndicateURL(feed->URL(), feed->Name());
	};
	
	return B_OK;
};

status_t FeedServer::HandleFlattenFeeds(BMessage */*msg*/) {
	SaveFeeds();

	return B_OK;
};

status_t FeedServer::HandleIntegrationTimeout(BMessage */*msg*/) {
	FailIntegrationTest("registration, refresh, and failure checks did not finish within 15 seconds");
	return B_OK;
};
