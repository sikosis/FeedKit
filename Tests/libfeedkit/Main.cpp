#include <libfeedkit/Channel.h>
#include <libfeedkit/Content.h>
#include <libfeedkit/DownloadProgress.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/ErrorDetails.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedHandler.h>
#include <libfeedkit/FeedListener.h>
#include <libfeedkit/Item.h>
#include <libfeedkit/Private/Constants.h>

#include <Roster.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

using namespace FeedKit;

class FeedServer {
	public:
		static DownloadProgress *CreateProgress(const char *mime, time_t start,
			int32 size, int32 downloaded, time_t end) {
			return new DownloadProgress(mime, start, size, downloaded, end);
		}

		static void SetProgress(Enclosure *enclosure, DownloadProgress *progress) {
			enclosure->SetProgress(progress);
		}

		static void SetState(Enclosure *enclosure, EnclosureState state) {
			enclosure->SetState(state);
		}
};

static int32 sChecks = 0;
static int32 sFailures = 0;

class CountingHandler : public FeedHandler {
	public:
		CountingHandler()
			: fServerStarted(0),
			fChannelUpdated(0),
			fItemRead(0),
			fEnclosureStarted(0) {
		}

		void ServerStarted() {
			fServerStarted++;
		}

		void ChannelUpdated(Feed *, Channel *channel) {
			if (channel != NULL)
				fChannelUpdated++;
		}

		void ItemRead(Feed *, Channel *channel, Item *item) {
			if (channel != NULL && item != NULL)
				fItemRead++;
		}

		void EnclosureDownloadStarted(Feed *, Item *item, Enclosure *enclosure) {
			if (item != NULL && enclosure != NULL)
				fEnclosureStarted++;
		}

		int32 fServerStarted;
		int32 fChannelUpdated;
		int32 fItemRead;
		int32 fEnclosureStarted;
};

static void
Check(bool condition, const char *description)
{
	sChecks++;
	if (!condition) {
		sFailures++;
		fprintf(stderr, "FAIL: %s\n", description);
	}
}

static void
CheckString(const char *actual, const char *expected, const char *description)
{
	Check(actual != NULL && strcmp(actual, expected) == 0, description);
}

template<typename Type>
static bool
RoundTrip(const Type &source, Type &copy, const char *description)
{
	ssize_t size = source.FlattenedSize();
	if (size <= 0) {
		Check(false, description);
		return false;
	}

	char *buffer = new char[size];
	status_t result = source.Flatten(buffer, size);
	if (result == B_OK)
		result = copy.Unflatten(source.TypeCode(), buffer, size);
	delete[] buffer;

	Check(result == B_OK, description);
	return result == B_OK;
}

static void
TestUUIDFixtures()
{
	Feed feed("https://example.com/feed.xml", "Example Feed");
	Channel *channel = new Channel();
	channel->Title("Example Channel");
	channel->Link("https://example.com/");
	channel->Description("Channel description");
	feed.AddChannel(channel);

	Item *item = new Item();
	item->Author("Alice");
	item->Title("Example Item");
	item->GUID("item-001");
	item->Link("https://example.com/items/1");
	channel->AddItem(item);

	Content *content = new Content();
	content->MIMEType("text/html");
	content->Summary("Summary");
	content->Text("Body text");
	item->AddContent(content);

	Enclosure enclosure("https://example.com/audio.ogg", "audio/ogg",
		"Episode audio", 12345);

	CheckString(feed.UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62",
		"Feed UUID fixture changed");
	CheckString(channel->UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62_"
		"8e6c1f5dd0c3ed0bf681a3b2af0273e82024d7d3",
		"Channel UUID fixture changed");
	CheckString(item->UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62_"
		"8e6c1f5dd0c3ed0bf681a3b2af0273e82024d7d3_"
		"9ae2bd068f9a1798073eeaea7b460a6c181d396d",
		"Item UUID fixture changed");
	CheckString(content->UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62_"
		"8e6c1f5dd0c3ed0bf681a3b2af0273e82024d7d3_"
		"9ae2bd068f9a1798073eeaea7b460a6c181d396d_"
		"c78535ec3ca789ba428bc2db3a91473889fb0955",
		"Content UUID fixture changed");
	CheckString(enclosure.UUID(),
		"b1dc6a5acb1a9ac64a91c6bd7497e7e053e82336",
		"Enclosure UUID fixture changed");
}

static void
TestModelRoundTrips()
{
	Feed feed("https://example.com/feed.xml", "Example Feed");
	(void)feed.UUID();
	Feed feedCopy;
	if (RoundTrip(feed, feedCopy, "Feed flatten/unflatten failed")) {
		CheckString(feedCopy.URL(), feed.URL(), "Feed URL did not round trip");
		CheckString(feedCopy.Name(), feed.Name(), "Feed name did not round trip");
		Check(feedCopy.Subscribed() == feed.Subscribed(),
			"Feed subscription did not round trip");
		CheckString(feedCopy.UUID(), feed.UUID(), "Feed UUID did not round trip");
	}

	Channel *channel = new Channel();
	channel->Title("Example Channel");
	channel->Link("https://example.com/");
	channel->Description("Channel description");
	channel->Language("en-AU");
	channel->Copyright("Example copyright");
	channel->Editor("editor@example.com");
	channel->Webmaster("webmaster@example.com");
	channel->PublishedDate(1700000000);
	channel->BuildDate(1700000100);
	channel->Category("Technology");
	channel->Generator("FeedKit tests");
	channel->Docs("https://example.com/docs");
	channel->TTL(30);
	channel->PICSRating("safe");
	feed.AddChannel(channel);
	(void)channel->UUID();
	Channel channelCopy;
	if (RoundTrip(*channel, channelCopy, "Channel flatten/unflatten failed")) {
		CheckString(channelCopy.Title(), channel->Title(), "Channel title did not round trip");
		CheckString(channelCopy.Link(), channel->Link(), "Channel link did not round trip");
		CheckString(channelCopy.Description(), channel->Description(),
			"Channel description did not round trip");
		CheckString(channelCopy.Language(), channel->Language(),
			"Channel language did not round trip");
		CheckString(channelCopy.Copyright(), channel->Copyright(),
			"Channel copyright did not round trip");
		CheckString(channelCopy.Editor(), channel->Editor(),
			"Channel editor did not round trip");
		CheckString(channelCopy.Webmaster(), channel->Webmaster(),
			"Channel webmaster did not round trip");
		Check(channelCopy.PublishedDate() == channel->PublishedDate(),
			"Channel published date did not round trip");
		Check(channelCopy.BuildDate() == channel->BuildDate(),
			"Channel build date did not round trip");
		CheckString(channelCopy.Category(), channel->Category(),
			"Channel category did not round trip");
		CheckString(channelCopy.Generator(), channel->Generator(),
			"Channel generator did not round trip");
		CheckString(channelCopy.Docs(), channel->Docs(),
			"Channel documentation URL did not round trip");
		Check(channelCopy.TTL() == channel->TTL(), "Channel TTL did not round trip");
		CheckString(channelCopy.PICSRating(), channel->PICSRating(),
			"Channel PICS rating did not round trip");
		CheckString(channelCopy.UUID(), channel->UUID(), "Channel UUID did not round trip");
	}

	Item *item = new Item();
	item->IsGUIDPermaLink(false);
	item->GUID("item-001");
	item->Author("Alice");
	item->Title("Example Item");
	item->Link("https://example.com/items/1");
	item->Category("News");
	item->Comments("https://example.com/items/1/comments");
	item->Date(1700000200);
	item->SourceURL("https://example.com/source");
	item->SourceTitle("Example source");
	channel->AddItem(item);
	(void)item->UUID();
	Item itemCopy;
	if (RoundTrip(*item, itemCopy, "Item flatten/unflatten failed")) {
		CheckString(itemCopy.GUID(), item->GUID(), "Item GUID did not round trip");
		CheckString(itemCopy.Author(), item->Author(), "Item author did not round trip");
		CheckString(itemCopy.Title(), item->Title(), "Item title did not round trip");
		CheckString(itemCopy.Link(), item->Link(), "Item link did not round trip");
		CheckString(itemCopy.Category(), item->Category(), "Item category did not round trip");
		CheckString(itemCopy.Comments(), item->Comments(), "Item comments did not round trip");
		Check(itemCopy.Date() == item->Date(), "Item date did not round trip");
		CheckString(itemCopy.SourceURL(), item->SourceURL(),
			"Item source URL did not round trip");
		CheckString(itemCopy.SourceTitle(), item->SourceTitle(),
			"Item source title did not round trip");
		CheckString(itemCopy.UUID(), item->UUID(), "Item UUID did not round trip");
	}

	Content *content = new Content();
	content->MIMEType("text/html");
	content->Summary("Summary");
	content->Text("Body text");
	item->AddContent(content);
	(void)content->UUID();
	Content contentCopy;
	if (RoundTrip(*content, contentCopy, "Content flatten/unflatten failed")) {
		CheckString(contentCopy.MIMEType(), content->MIMEType(),
			"Content MIME type did not round trip");
		CheckString(contentCopy.Summary(), content->Summary(),
			"Content summary did not round trip");
		CheckString(contentCopy.Text(), content->Text(), "Content text did not round trip");
		CheckString(contentCopy.UUID(), content->UUID(), "Content UUID did not round trip");
	}

	Enclosure enclosure("https://example.com/audio.ogg", "audio/ogg",
		"Episode audio", 12345);
	(void)enclosure.UUID();
	Enclosure enclosureCopy;
	if (RoundTrip(enclosure, enclosureCopy, "Enclosure flatten/unflatten failed")) {
		CheckString(enclosureCopy.URL(), enclosure.URL(), "Enclosure URL did not round trip");
		CheckString(enclosureCopy.MIME(), enclosure.MIME(), "Enclosure MIME did not round trip");
		CheckString(enclosureCopy.Description(), enclosure.Description(),
			"Enclosure description did not round trip");
		Check(enclosureCopy.Size() == enclosure.Size(), "Enclosure size did not round trip");
		CheckString(enclosureCopy.UUID(), enclosure.UUID(),
			"Enclosure UUID did not round trip");
	}

	ErrorDetails error(42, "Example error");
	ErrorDetails errorCopy;
	if (RoundTrip(error, errorCopy, "ErrorDetails flatten/unflatten failed")) {
		Check(errorCopy.Code() == error.Code(), "ErrorDetails code did not round trip");
		CheckString(errorCopy.Message(), error.Message(),
			"ErrorDetails message did not round trip");
	}
}

static void
TestModelOwnership()
{
	Feed source("https://example.com/owned.xml", "Owned tree");
	Channel *sourceChannel = new Channel();
	sourceChannel->Title("Owned channel");
	sourceChannel->Link("https://example.com/owned");
	sourceChannel->Description("Ownership fixture");
	source.AddChannel(sourceChannel);

	Item *sourceItem = new Item();
	sourceItem->Author("Owner");
	sourceItem->Title("Owned item");
	sourceItem->GUID("owned-item");
	sourceChannel->AddItem(sourceItem);

	Content *sourceContent = new Content();
	sourceContent->MIMEType("text/plain");
	sourceContent->Text("Owned content");
	sourceItem->AddContent(sourceContent);
	sourceItem->AddEnclosure(new Enclosure("https://example.com/owned.ogg",
		"audio/ogg", "Owned enclosure", 512));
	sourceChannel->Image(new Enclosure("https://example.com/icon.png",
		"image/png", "Owned icon", 128));

	Feed copied(source);
	Channel *copiedChannel = copied.ChannelAt(0);
	Item *copiedItem = copiedChannel == NULL ? NULL : copiedChannel->ItemAt(0);
	Content *copiedContent = copiedItem == NULL ? NULL : copiedItem->ContentAt(0);
	Check(copiedChannel != NULL && copiedChannel != sourceChannel,
		"Feed copy did not own an independent channel");
	Check(copiedChannel != NULL && copiedChannel->ParentFeed() == &copied,
		"Copied channel has the wrong parent feed");
	Check(copiedItem != NULL && copiedItem != sourceItem,
		"Channel copy did not own an independent item");
	Check(copiedItem != NULL && copiedItem->ParentChannel() == copiedChannel,
		"Copied item has the wrong parent channel");
	Check(copiedContent != NULL && copiedContent != sourceContent,
		"Item copy did not own independent content");
	Check(copiedContent != NULL && copiedContent->ParentItem() == copiedItem,
		"Copied content has the wrong parent item");
	Check(copiedItem != NULL
		&& copiedItem->EnclosureAt(0) != sourceItem->EnclosureAt(0),
		"Item copy did not own an independent enclosure");
	Check(copiedChannel != NULL && copiedChannel->Image() != sourceChannel->Image(),
		"Channel copy did not own an independent image");

	sourceItem->Title("Changed source item");
	Check(copiedItem != NULL && strcmp(copiedItem->Title(), "Owned item") == 0,
		"Mutating the source changed its deep copy");

	Feed assigned;
	assigned = source;
	Channel *assignedFeedChannel = assigned.ChannelAt(0);
	Check(assignedFeedChannel != NULL && assignedFeedChannel != sourceChannel,
		"Feed assignment retained a source child pointer");
	Check(assignedFeedChannel != NULL
		&& assignedFeedChannel->ParentFeed() == &assigned,
		"Assigned channel has the wrong parent feed");

	Channel assignedChannel;
	assignedChannel = *sourceChannel;
	Item *assignedChannelItem = assignedChannel.ItemAt(0);
	Check(assignedChannelItem != NULL && assignedChannelItem != sourceItem,
		"Channel assignment retained a source item pointer");
	Check(assignedChannelItem != NULL
		&& assignedChannelItem->ParentChannel() == &assignedChannel,
		"Channel assignment did not repair the item parent");

	Item assignedItem;
	assignedItem = *sourceItem;
	Content *assignedItemContent = assignedItem.ContentAt(0);
	Check(assignedItemContent != NULL && assignedItemContent != sourceContent,
		"Item assignment retained a source content pointer");
	Check(assignedItemContent != NULL
		&& assignedItemContent->ParentItem() == &assignedItem,
		"Item assignment did not repair the content parent");

	Enclosure *sourceEnclosure = sourceItem->EnclosureAt(0);
	DownloadProgress *ownershipProgress = FeedServer::CreateProgress(
		"audio/ogg", time(NULL) - 1, 512, 256, 0);
	FeedServer::SetProgress(sourceEnclosure, ownershipProgress);
	delete ownershipProgress;
	Enclosure assignedEnclosure;
	assignedEnclosure = *sourceEnclosure;
	Check(assignedEnclosure.Progress() != sourceEnclosure->Progress(),
		"Enclosure assignment retained a source progress pointer");
	Check(assignedEnclosure.Progress() != NULL
		&& assignedEnclosure.Progress()->AmountDownloaded() == 256,
		"Enclosure assignment lost its progress values");

	Feed restored;
	if (RoundTrip(source, restored, "Owned model tree did not round trip")) {
		Channel *restoredChannel = restored.ChannelAt(0);
		Item *restoredItem = restoredChannel == NULL
			? NULL : restoredChannel->ItemAt(0);
		Check(restored.ChannelCount() == 1,
			"Owned model round trip lost its channel");
		Check(restoredChannel != NULL
			&& restoredChannel->ParentFeed() == &restored,
			"Round-tripped channel has the wrong parent feed");
		Check(restoredItem != NULL
			&& restoredItem->ParentChannel() == restoredChannel,
			"Round-tripped item has the wrong parent channel");
	}
	if (RoundTrip(source, restored, "Reused model did not unflatten")) {
		Check(restored.ChannelCount() == 1,
			"Repeated unflatten appended duplicate children");
	}
}

static void
TestDownloadProgress()
{
	time_t now = time(NULL);
	DownloadProgress *progress = FeedServer::CreateProgress("audio/ogg", now - 10,
		1000, 1000, now);
	Check(progress->IsComplete(), "Completed progress was not marked complete");
	Check(progress->PercentageComplete() == 1.0f,
		"Progress percentage was calculated incorrectly");
	DownloadProgress emptyProgress;
	Check(emptyProgress.PercentageComplete() == 0.0f,
		"Empty progress should report zero percent complete");

	DownloadProgress copied(progress);
	CheckString(copied.MIME(), progress->MIME(), "Copied progress lost its MIME type");
	Check(copied.StartTime() == progress->StartTime(), "Copied progress lost its start time");
	Check(copied.EndTime() == progress->EndTime(), "Copied progress lost its end time");
	Check(copied.Size() == progress->Size(), "Copied progress lost its size");
	Check(copied.AmountDownloaded() == progress->AmountDownloaded(),
		"Copied progress lost its downloaded amount");
	Check(copied.IsComplete() == progress->IsComplete(),
		"Copied progress lost its completion state");

	DownloadProgress roundTrip;
	if (RoundTrip(*progress, roundTrip, "DownloadProgress flatten/unflatten failed")) {
		CheckString(roundTrip.MIME(), progress->MIME(),
			"Progress MIME type did not round trip");
		Check(roundTrip.StartTime() == progress->StartTime(),
			"Progress start time did not round trip");
		Check(roundTrip.EndTime() == progress->EndTime(),
			"Progress end time did not round trip");
		Check(roundTrip.Size() == progress->Size(), "Progress size did not round trip");
		Check(roundTrip.AmountDownloaded() == progress->AmountDownloaded(),
			"Progress downloaded amount did not round trip");
		Check(roundTrip.IsComplete(), "Progress completion state did not survive serialization");
	}

	Enclosure enclosure("https://example.com/audio.ogg", "audio/ogg",
		"Episode audio", 1000);
	FeedServer::SetState(&enclosure, Completed);
	FeedServer::SetProgress(&enclosure, progress);
	delete progress;

	Check(enclosure.Progress() != NULL, "Enclosure discarded its progress update");
	Check(enclosure.Progress() != NULL && enclosure.Progress()->IsComplete(),
		"Enclosure progress copy lost its completion state");

	Enclosure copiedEnclosure(enclosure);
	Check(copiedEnclosure.Progress() != NULL,
		"Copied enclosure lost its progress object");
	Check(copiedEnclosure.Progress() != NULL
		&& copiedEnclosure.Progress()->AmountDownloaded() == 1000,
		"Copied enclosure lost its progress values");

	Enclosure roundTripEnclosure;
	if (RoundTrip(enclosure, roundTripEnclosure,
		"Enclosure progress flatten/unflatten failed")) {
		Check(roundTripEnclosure.State() == Completed,
			"Enclosure state did not round trip");
		Check(roundTripEnclosure.Progress() != NULL,
			"Enclosure progress did not round trip");
		Check(roundTripEnclosure.Progress() != NULL
			&& roundTripEnclosure.Progress()->AmountDownloaded() == 1000,
			"Enclosure progress values did not round trip");
	}
}

static void
TestFeedListenerSafety()
{
	FeedListener *listener = new FeedListener();
	CountingHandler registered;
	CountingHandler unknown;

	listener->AddHandler(NULL);
	listener->AddHandler(&registered);
	listener->AddHandler(&registered);
	listener->RemoveHandler(&unknown);

	BMessage launched(B_SOME_APP_LAUNCHED);
	launched.AddString("be:signature", FeedKit::ServerSignature);
	listener->MessageReceived(&launched);
	Check(registered.fServerStarted == 1,
		"FeedListener registered the same handler more than once");

	listener->RemoveHandler(&registered);
	listener->MessageReceived(&launched);
	Check(registered.fServerStarted == 1,
		"FeedListener called a removed handler");
	listener->AddHandler(&registered);

	Feed feed("https://example.com/listener.xml", "Listener fixture");
	Channel *channel = new Channel();
	channel->Title("Listener channel");
	channel->Link("https://example.com/listener");
	channel->Description("Listener channel fixture");
	feed.AddChannel(channel);
	Item *item = new Item();
	item->Author("Listener test");
	item->Title("Listener item");
	item->GUID("listener-item");
	item->Link("https://example.com/listener/item");
	channel->AddItem(item);
	Enclosure *enclosure = new Enclosure("https://example.com/listener.ogg",
		"audio/ogg", "Listener enclosure", 100);
	item->AddEnclosure(enclosure);

	BString channelUUID(channel->UUID());
	BString itemUUID(item->UUID());
	BString enclosureUUID(enclosure->UUID());

	BMessage channelUpdated(FeedKit::Private::FromServer::ChannelUpdated);
	channelUpdated.AddFlat("feed", &feed);
	channelUpdated.AddString("channel", channelUUID);
	listener->MessageReceived(&channelUpdated);
	Check(registered.fChannelUpdated == 1,
		"FeedListener did not reconstruct a valid channel update");

	BMessage missingChannel(FeedKit::Private::FromServer::ChannelUpdated);
	missingChannel.AddFlat("feed", &feed);
	missingChannel.AddString("channel", "missing-channel");
	listener->MessageReceived(&missingChannel);
	Check(registered.fChannelUpdated == 1,
		"FeedListener dispatched an invalid channel update");

	BMessage itemRead(FeedKit::Private::FromServer::ItemRead);
	itemRead.AddFlat("feed", &feed);
	itemRead.AddString("channel", channelUUID);
	itemRead.AddString("item", itemUUID);
	listener->MessageReceived(&itemRead);
	Check(registered.fItemRead == 1,
		"FeedListener did not reconstruct a valid item update");

	BMessage missingItem(FeedKit::Private::FromServer::ItemRead);
	missingItem.AddFlat("feed", &feed);
	missingItem.AddString("channel", channelUUID);
	missingItem.AddString("item", "missing-item");
	listener->MessageReceived(&missingItem);
	Check(registered.fItemRead == 1,
		"FeedListener dispatched an invalid item update");

	BMessage enclosureStarted(
		FeedKit::Private::FromServer::DownloadEnclosureStarted);
	enclosureStarted.AddFlat("feed", &feed);
	enclosureStarted.AddString("channel", channelUUID);
	enclosureStarted.AddString("item", itemUUID);
	enclosureStarted.AddString("enclosure", enclosureUUID);
	listener->MessageReceived(&enclosureStarted);
	Check(registered.fEnclosureStarted == 1,
		"FeedListener did not reconstruct a valid enclosure update");

	BMessage missingEnclosure(
		FeedKit::Private::FromServer::DownloadEnclosureStarted);
	missingEnclosure.AddFlat("feed", &feed);
	missingEnclosure.AddString("channel", channelUUID);
	missingEnclosure.AddString("item", itemUUID);
	missingEnclosure.AddString("enclosure", "missing-enclosure");
	listener->MessageReceived(&missingEnclosure);
	Check(registered.fEnclosureStarted == 1,
		"FeedListener dispatched an invalid enclosure update");

	Check(listener->RefreshFeed(NULL) == B_BAD_VALUE,
		"FeedListener accepted a null feed refresh");
	Check(listener->SendMessage(NULL) == B_BAD_VALUE,
		"FeedListener accepted a null message");
	Check(listener->MarkItemRead(NULL) == B_BAD_VALUE,
		"FeedListener accepted a null item update");
	Check(listener->RegisterFeed(NULL) == B_BAD_VALUE,
		"FeedListener accepted a null registration URL");
	Check(listener->ChannelIconPath(NULL) == NULL,
		"FeedListener accepted a null icon channel");
	Check(listener->DownloadEnclosure(NULL, enclosure, "/tmp") == B_BAD_VALUE,
		"FeedListener accepted a null enclosure item");
	Check(listener->CancelEnclosureDownload(item, NULL) == B_BAD_VALUE,
		"FeedListener accepted a null enclosure cancellation");

	listener->RemoveHandler(&registered);
	BMessenger(listener).SendMessage(B_QUIT_REQUESTED);
}

int
main()
{
	TestUUIDFixtures();
	TestModelRoundTrips();
	TestModelOwnership();
	TestDownloadProgress();
	TestFeedListenerSafety();

	if (sFailures == 0) {
		printf("FeedKit unit tests passed: %d checks.\n", (int)sChecks);
		return 0;
	}

	fprintf(stderr, "FeedKit unit tests failed: %d of %d checks failed.\n",
		(int)sFailures, (int)sChecks);
	return 1;
}
