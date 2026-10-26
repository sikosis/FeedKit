#include <libfeedkit/Channel.h>
#include <libfeedkit/Content.h>
#include <libfeedkit/DownloadProgress.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/ErrorDetails.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/Item.h>

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
	Channel channel;
	channel.Title("Example Channel");
	channel.Link("https://example.com/");
	channel.Description("Channel description");
	feed.AddChannel(&channel);

	Item item;
	item.Author("Alice");
	item.Title("Example Item");
	item.GUID("item-001");
	item.Link("https://example.com/items/1");
	channel.AddItem(&item);

	Content content;
	content.MIMEType("text/html");
	content.Summary("Summary");
	content.Text("Body text");
	item.AddContent(&content);

	Enclosure enclosure("https://example.com/audio.ogg", "audio/ogg",
		"Episode audio", 12345);

	CheckString(feed.UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62",
		"Feed UUID fixture changed");
	CheckString(channel.UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62_"
		"8e6c1f5dd0c3ed0bf681a3b2af0273e82024d7d3",
		"Channel UUID fixture changed");
	CheckString(item.UUID(),
		"6904f1c4283b1a2c822ade42a644da65a694eb62_"
		"8e6c1f5dd0c3ed0bf681a3b2af0273e82024d7d3_"
		"9ae2bd068f9a1798073eeaea7b460a6c181d396d",
		"Item UUID fixture changed");
	CheckString(content.UUID(),
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

	Channel channel;
	channel.Title("Example Channel");
	channel.Link("https://example.com/");
	channel.Description("Channel description");
	channel.Language("en-AU");
	channel.Copyright("Example copyright");
	channel.Editor("editor@example.com");
	channel.Webmaster("webmaster@example.com");
	channel.PublishedDate(1700000000);
	channel.BuildDate(1700000100);
	channel.Category("Technology");
	channel.Generator("FeedKit tests");
	channel.Docs("https://example.com/docs");
	channel.TTL(30);
	channel.PICSRating("safe");
	feed.AddChannel(&channel);
	(void)channel.UUID();
	Channel channelCopy;
	if (RoundTrip(channel, channelCopy, "Channel flatten/unflatten failed")) {
		CheckString(channelCopy.Title(), channel.Title(), "Channel title did not round trip");
		CheckString(channelCopy.Link(), channel.Link(), "Channel link did not round trip");
		CheckString(channelCopy.Description(), channel.Description(),
			"Channel description did not round trip");
		CheckString(channelCopy.Language(), channel.Language(),
			"Channel language did not round trip");
		CheckString(channelCopy.Copyright(), channel.Copyright(),
			"Channel copyright did not round trip");
		CheckString(channelCopy.Editor(), channel.Editor(),
			"Channel editor did not round trip");
		CheckString(channelCopy.Webmaster(), channel.Webmaster(),
			"Channel webmaster did not round trip");
		Check(channelCopy.PublishedDate() == channel.PublishedDate(),
			"Channel published date did not round trip");
		Check(channelCopy.BuildDate() == channel.BuildDate(),
			"Channel build date did not round trip");
		CheckString(channelCopy.Category(), channel.Category(),
			"Channel category did not round trip");
		CheckString(channelCopy.Generator(), channel.Generator(),
			"Channel generator did not round trip");
		CheckString(channelCopy.Docs(), channel.Docs(),
			"Channel documentation URL did not round trip");
		Check(channelCopy.TTL() == channel.TTL(), "Channel TTL did not round trip");
		CheckString(channelCopy.PICSRating(), channel.PICSRating(),
			"Channel PICS rating did not round trip");
		CheckString(channelCopy.UUID(), channel.UUID(), "Channel UUID did not round trip");
	}

	Item item;
	item.IsGUIDPermaLink(false);
	item.GUID("item-001");
	item.Author("Alice");
	item.Title("Example Item");
	item.Link("https://example.com/items/1");
	item.Category("News");
	item.Comments("https://example.com/items/1/comments");
	item.Date(1700000200);
	item.SourceURL("https://example.com/source");
	item.SourceTitle("Example source");
	channel.AddItem(&item);
	(void)item.UUID();
	Item itemCopy;
	if (RoundTrip(item, itemCopy, "Item flatten/unflatten failed")) {
		CheckString(itemCopy.GUID(), item.GUID(), "Item GUID did not round trip");
		CheckString(itemCopy.Author(), item.Author(), "Item author did not round trip");
		CheckString(itemCopy.Title(), item.Title(), "Item title did not round trip");
		CheckString(itemCopy.Link(), item.Link(), "Item link did not round trip");
		CheckString(itemCopy.Category(), item.Category(), "Item category did not round trip");
		CheckString(itemCopy.Comments(), item.Comments(), "Item comments did not round trip");
		Check(itemCopy.Date() == item.Date(), "Item date did not round trip");
		CheckString(itemCopy.SourceURL(), item.SourceURL(),
			"Item source URL did not round trip");
		CheckString(itemCopy.SourceTitle(), item.SourceTitle(),
			"Item source title did not round trip");
		CheckString(itemCopy.UUID(), item.UUID(), "Item UUID did not round trip");
	}

	Content content;
	content.MIMEType("text/html");
	content.Summary("Summary");
	content.Text("Body text");
	item.AddContent(&content);
	(void)content.UUID();
	Content contentCopy;
	if (RoundTrip(content, contentCopy, "Content flatten/unflatten failed")) {
		CheckString(contentCopy.MIMEType(), content.MIMEType(),
			"Content MIME type did not round trip");
		CheckString(contentCopy.Summary(), content.Summary(),
			"Content summary did not round trip");
		CheckString(contentCopy.Text(), content.Text(), "Content text did not round trip");
		CheckString(contentCopy.UUID(), content.UUID(), "Content UUID did not round trip");
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

int
main()
{
	TestUUIDFixtures();
	TestModelRoundTrips();
	TestDownloadProgress();

	if (sFailures == 0) {
		printf("FeedKit unit tests passed: %d checks.\n", (int)sChecks);
		return 0;
	}

	fprintf(stderr, "FeedKit unit tests failed: %d of %d checks failed.\n",
		(int)sFailures, (int)sChecks);
	return 1;
}
