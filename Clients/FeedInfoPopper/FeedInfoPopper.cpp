#include "FeedInfoPopper.h"

#include <Messenger.h>
#include <Notification.h>
#include <OS.h>

#include <libfeedkit/Channel.h>
#include <libfeedkit/Content.h>
#include <libfeedkit/ContentSpecification.h>
#include <libfeedkit/DownloadProgress.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedListener.h>
#include <libfeedkit/Item.h>
#include <libfeedkit/ItemSpecification.h>
#include <libfeedkit/SettingsManager.h>

#include <stdio.h>

using namespace FeedKit;

const char *kAppSignature = "application/x-vnd.beclan.feedkit.InfoPopper";

FeedInfoPopper::FeedInfoPopper(bool smokeTest)
	: BApplication(kAppSignature),
	fSmokeTest(smokeTest),
	fFeedListener(NULL)
{
}

FeedInfoPopper::~FeedInfoPopper()
{
	StopListener();
}

void
FeedInfoPopper::ReadyToRun()
{
	if (fSmokeTest) {
		BNotification notification(B_INFORMATION_NOTIFICATION);
		notification.SetGroup("FeedKit");
		notification.SetTitle("FeedKit notification test");
		notification.SetContent("Native notification construction passed.");
		notification.SetMessageID("feedkit-smoke-test");
		printf("FeedKit notification smoke test passed\n");
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fFeedListener = new FeedListener();
	fFeedListener->AddHandler(this);
	fFeedListener->StartListening();

	FeedKit::Settings::SettingsManager manager(
		FeedKit::Settings::AppTypes::SettingClient, "Info Popper");
	BMessage settings;
	manager.Template(&settings, kAppSignature);
}

void
FeedInfoPopper::FeedRegistered(Feed *feed)
{
	for (uint32 index = 0; index < feed->ChannelCount(); index++)
		BroadcastChannelContents(feed->ChannelAt(index));
}

void
FeedInfoPopper::ChannelUpdated(Feed */*feed*/, Channel *channel)
{
	BroadcastChannelContents(channel);
}

void
FeedInfoPopper::EnclosureDownloadProgress(Feed */*feed*/, Item */*item*/,
	Enclosure *enclosure)
{
	BroadcastDownloadProgress(enclosure);
}

void
FeedInfoPopper::BroadcastChannelContents(Channel *channel)
{
	ItemList newItems = channel->FindItems(new NewItemSpecification(), true);
	for (ItemList::iterator iterator = newItems.begin();
		iterator != newItems.end(); iterator++) {
		Item *item = *iterator;
		if (item == NULL)
			continue;

		BString content(channel->Title());
		content << " - " << item->Title();

		ContentList itemContents = item->FindContent(
			new MIMEContentSpecification("text/plain"), true);
		if (itemContents.empty())
			itemContents = item->FindContent(
				new MIMESuperTypeContentSpecification("text/"), true);
		if (itemContents.empty())
			itemContents = item->FindContent(new AllContentSpecification(), true);
		if (!itemContents.empty())
			content << "\n" << itemContents[0]->Text();

		BNotification notification(B_INFORMATION_NOTIFICATION);
		notification.SetGroup("FeedKit");
		notification.SetTitle("New feed item");
		notification.SetContent(content.String());
		notification.SetMessageID(item->GUID());
		notification.Send();
	}
}

void
FeedInfoPopper::BroadcastDownloadProgress(Enclosure *enclosure)
{
	const DownloadProgress *progress = enclosure->Progress();
	if (progress == NULL)
		return;

	BString content;
	content << enclosure->Description() << "\n" << enclosure->URL();

	BNotification notification(B_PROGRESS_NOTIFICATION);
	notification.SetGroup("FeedKit");
	notification.SetTitle("Downloading enclosure");
	notification.SetContent(content.String());
	notification.SetMessageID(enclosure->UUID());
	notification.SetProgress(progress->PercentageComplete());
	notification.Send();
}

void
FeedInfoPopper::StopListener()
{
	if (fFeedListener == NULL)
		return;

	thread_id thread = fFeedListener->Thread();
	fFeedListener->RemoveHandler(this);
	fFeedListener->StopListening();
	BMessenger(fFeedListener).SendMessage(B_QUIT_REQUESTED);
	if (thread >= B_OK) {
		status_t result;
		wait_for_thread(thread, &result);
	}
	fFeedListener = NULL;
}
