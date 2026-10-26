#include <File.h>
#include <Message.h>
#include <image.h>

#include <libfeedkit/Channel.h>
#include <libfeedkit/Content.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedParser.h>
#include <libfeedkit/Item.h>

#include "ParserManager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

using namespace FeedKit;

static int32 sChecks = 0;
static int32 sFailures = 0;

typedef FeedParser *(*instantiate_parser_func)(BMessage);

struct LoadedParser {
	image_id image;
	FeedParser *parser;
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

static bool
ReadFile(const char *path, char **buffer, int32 *size)
{
	BFile file(path, B_READ_ONLY);
	off_t fileSize = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&fileSize) != B_OK
		|| fileSize < 0 || fileSize > INT32_MAX)
		return false;

	char *data = new char[fileSize == 0 ? 1 : fileSize];
	ssize_t bytesRead = file.Read(data, fileSize);
	if (bytesRead != fileSize) {
		delete[] data;
		return false;
	}

	*buffer = data;
	*size = static_cast<int32>(fileSize);
	return true;
}

static LoadedParser
LoadParser(const char *path)
{
	LoadedParser loaded = { B_ERROR, NULL };
	loaded.image = load_add_on(path);
	Check(loaded.image >= B_OK, "Parser add-on could not be loaded");
	if (loaded.image < B_OK)
		return loaded;

	instantiate_parser_func instantiate = NULL;
	status_t status = get_image_symbol(loaded.image, "instantiate_parser",
		B_SYMBOL_TYPE_TEXT, reinterpret_cast<void **>(&instantiate));
	Check(status == B_OK && instantiate != NULL,
		"Parser factory symbol could not be loaded");
	if (status != B_OK || instantiate == NULL)
		return loaded;

	loaded.parser = instantiate(BMessage());
	Check(loaded.parser != NULL, "Parser factory returned NULL");
	return loaded;
}

static void
UnloadParser(LoadedParser *loaded)
{
	delete loaded->parser;
	loaded->parser = NULL;
	if (loaded->image >= B_OK) {
		Check(unload_add_on(loaded->image) == B_OK,
			"Parser add-on could not be unloaded");
		loaded->image = B_ERROR;
	}
}

static void
CheckValidFeed(FeedParser *parser, const char *url, const char *data,
	int32 size, const char *title, const char *itemTitle)
{
	Check(parser->HandlesFeed(url, data, size) > 0.0f,
		"Parser rejected its valid fixture");
	Feed *feed = parser->ParseFeed(url, data, size);
	Check(feed != NULL, "Parser did not return a valid fixture");
	if (feed != NULL) {
		Check(feed->ChannelCount() == 1,
			"Valid fixture did not produce one channel");
		Channel *channel = feed->ChannelAt(0);
		Check(channel != NULL && strcmp(channel->Title(), title) == 0,
			"Valid fixture channel title changed");
		Check(channel != NULL && channel->ItemCount() == 1
			&& channel->ItemAt(0) != NULL
			&& strcmp(channel->ItemAt(0)->Title(), itemTitle) == 0,
			"Valid fixture item changed");
	}
	delete feed;
}

static void
CheckMalformedFeed(FeedParser *parser, const char *url,
	const char *malformed)
{
	int32 size = strlen(malformed);
	Check(parser->HandlesFeed(url, malformed, size) == 0.0f,
		"Parser claimed a malformed fixture");
	Feed *feed = parser->ParseFeed(url, malformed, size);
	Check(feed == NULL, "Parser returned a feed for malformed XML");
	delete feed;
}

static void
CheckIncompleteRSS(FeedParser *parser)
{
	const char *data =
		"<?xml version=\"1.0\"?><rss version=\"2.0\"><channel>"
		"<title>Incomplete RSS</title><item><title>Incomplete item</title>"
		"<guid>incomplete-rss-item</guid>"
		"<enclosure url=\"https://example.invalid/media\"/>"
		"<source>Source without URL</source></item></channel></rss>";
	int32 size = strlen(data);
	Check(parser->HandlesFeed("memory://incomplete.rss", data, size) > 0.0f,
		"RSS parser rejected an incomplete but well-formed feed");
	Feed *feed = parser->ParseFeed("memory://incomplete.rss", data, size);
	Check(feed != NULL, "RSS parser failed an incomplete feed");
	Channel *channel = feed == NULL ? NULL : feed->ChannelAt(0);
	Check(channel != NULL && channel->ItemCount() == 1,
		"Incomplete RSS item was not retained");
	Item *item = channel == NULL ? NULL : channel->ItemAt(0);
	Check(item != NULL && item->EnclosureCount() == 1,
		"Incomplete RSS enclosure was not retained safely");
	Check(item != NULL && item->EnclosureAt(0) != NULL
		&& item->EnclosureAt(0)->Size() == -1,
		"Missing RSS enclosure length did not use the unknown size");
	Check(item != NULL && !item->IsGUIDPermaLink(),
		"Missing RSS permalink flag did not use the safe default");
	delete feed;
}

static void
CheckIncompleteAtom(FeedParser *parser)
{
	const char *data =
		"<?xml version=\"1.0\"?><feed xmlns=\"http://www.w3.org/2005/Atom\">"
		"<title>Incomplete Atom</title><category/><entry>"
		"<title>Incomplete item</title><link/><content>content</content>"
		"</entry></feed>";
	int32 size = strlen(data);
	Check(parser->HandlesFeed("memory://incomplete.atom", data, size) > 0.0f,
		"Atom parser rejected an incomplete but well-formed feed");
	Feed *feed = parser->ParseFeed("memory://incomplete.atom", data, size);
	Check(feed != NULL, "Atom parser failed an incomplete feed");
	Channel *channel = feed == NULL ? NULL : feed->ChannelAt(0);
	Check(channel != NULL && channel->ItemCount() == 1,
		"Incomplete Atom entry was not retained");
	Item *item = channel == NULL ? NULL : channel->ItemAt(0);
	Check(item != NULL && strcmp(item->Link(), "") == 0,
		"Missing Atom link attributes did not use safe defaults");
	Check(item != NULL && item->ContentCount() == 1
		&& strcmp(item->ContentAt(0)->MIMEType(), "") == 0,
		"Missing Atom content type did not use the safe default");
	delete feed;
}

static void
CheckEncodedRSS(FeedParser *parser)
{
	const char data[] =
		"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>"
		"<rss version=\"2.0\"><channel><title>Caf\xe9</title>"
		"<item><title>Cr\xe8me br\xfbl\xe9" "e</title></item>"
		"</channel></rss>";
	int32 size = sizeof(data) - 1;
	Check(parser->HandlesFeed("memory://encoded.rss", data, size) > 0.0f,
		"RSS parser rejected an ISO-8859-1 feed");
	Feed *feed = parser->ParseFeed("memory://encoded.rss", data, size);
	Check(feed != NULL, "RSS parser failed an ISO-8859-1 feed");
	Channel *channel = feed == NULL ? NULL : feed->ChannelAt(0);
	Check(channel != NULL && strcmp(channel->Title(), "Caf\xc3\xa9") == 0,
		"RSS parser did not normalize an ISO-8859-1 channel title");
	Check(channel != NULL && channel->ItemCount() == 1
		&& strcmp(channel->ItemAt(0)->Title(),
			"Cr\xc3\xa8me br\xc3\xbbl\xc3\xa9" "e") == 0,
		"RSS parser did not normalize an ISO-8859-1 item title");
	delete feed;
}

static void
CheckEncodedAtom(FeedParser *parser)
{
	const char data[] =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<feed xmlns=\"http://www.w3.org/2005/Atom\">"
		"<title>\xe6\x97\xa5\xe6\x9c\xac</title><entry>"
		"<title>\xe6\x9b\xb4\xe6\x96\xb0</title></entry></feed>";
	int32 size = sizeof(data) - 1;
	Check(parser->HandlesFeed("memory://encoded.atom", data, size) > 0.0f,
		"Atom parser rejected a UTF-8 feed");
	Feed *feed = parser->ParseFeed("memory://encoded.atom", data, size);
	Check(feed != NULL, "Atom parser failed a UTF-8 feed");
	Channel *channel = feed == NULL ? NULL : feed->ChannelAt(0);
	Check(channel != NULL
		&& strcmp(channel->Title(), "\xe6\x97\xa5\xe6\x9c\xac") == 0,
		"Atom parser changed a UTF-8 channel title");
	Check(channel != NULL && channel->ItemCount() == 1
		&& strcmp(channel->ItemAt(0)->Title(),
			"\xe6\x9b\xb4\xe6\x96\xb0") == 0,
		"Atom parser changed a UTF-8 item title");
	delete feed;
}

int
main(int argc, char **argv)
{
	if (argc != 6) {
		fprintf(stderr, "Usage: %s RSS_ADDON ATOM_ADDON RSS_FIXTURE ATOM_FIXTURE PARSER_DIRECTORY\n",
			argv[0]);
		return 1;
	}

	setenv("FEEDKIT_INTEGRATION_TEST", "1", 1);
	char *rssData = NULL;
	char *atomData = NULL;
	int32 rssSize = 0;
	int32 atomSize = 0;
	Check(ReadFile(argv[3], &rssData, &rssSize),
		"Could not read the RSS fixture");
	Check(ReadFile(argv[4], &atomData, &atomSize),
		"Could not read the Atom fixture");

	LoadedParser rss = LoadParser(argv[1]);
	LoadedParser atom = LoadParser(argv[2]);
	if (rss.parser != NULL && rssData != NULL) {
		CheckValidFeed(rss.parser, "memory://integration.rss", rssData, rssSize,
			"FeedKit RSS integration fixture", "RSS fixture item");
		CheckMalformedFeed(rss.parser, "memory://malformed.rss",
			"<rss><channel><title>broken</channel>");
		CheckIncompleteRSS(rss.parser);
		CheckEncodedRSS(rss.parser);
	}
	if (atom.parser != NULL && atomData != NULL) {
		CheckValidFeed(atom.parser, "memory://integration.atom", atomData,
			atomSize, "FeedKit Atom integration fixture", "Atom fixture item");
		CheckMalformedFeed(atom.parser, "memory://malformed.atom",
			"<feed><title>broken</feed>");
		CheckIncompleteAtom(atom.parser);
		CheckEncodedAtom(atom.parser);
	}

	delete[] rssData;
	delete[] atomData;
	UnloadParser(&rss);
	UnloadParser(&atom);

	int32 discoveredCount = 0;
	{
		ParserManager manager(argv[5]);
		discoveredCount = manager.CountParsers();
		Check(discoveredCount >= 2,
			"Parser manager did not discover RSS and Atom add-ons");
		char *managerData = NULL;
		int32 managerSize = 0;
		Check(ReadFile(argv[3], &managerData, &managerSize),
			"Could not reread the parser-manager fixture");
		Feed *feed = managerData == NULL ? NULL : manager.Parse(
			"memory://manager.rss", managerData, managerSize);
		Check(feed != NULL && feed->ChannelCount() == 1,
			"Parser manager could not parse through a discovered add-on");
		delete feed;
		delete[] managerData;
	}
	{
		path_t duplicatePaths;
		duplicatePaths.push_back(argv[5]);
		duplicatePaths.push_back(argv[5]);
		ParserManager manager(duplicatePaths);
		Check(manager.CountParsers() == discoveredCount,
			"Duplicate parser directories changed the loaded parser set");
	}

	if (sFailures == 0) {
		printf("FeedKit parser tests passed: %d checks.\n", (int)sChecks);
		return 0;
	}

	fprintf(stderr, "FeedKit parser tests failed: %d of %d checks failed.\n",
		(int)sFailures, (int)sChecks);
	return 1;
}
