#include "TWindow.h"

#include <Application.h>
#include <Button.h>
#include <ListView.h>
#include <Messenger.h>
#include <OS.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <TextView.h>
#include <View.h>

#include <libfeedkit/Channel.h>
#include <libfeedkit/Content.h>
#include <libfeedkit/Feed.h>
#include <libfeedkit/FeedListener.h>
#include <libfeedkit/Item.h>

using namespace FeedKit;

const uint32 kMsgReloadFeeds = 'amrf';
const uint32 kMsgChannelSelected = 'amcs';
const uint32 kMsgItemSelected = 'amis';
const uint32 kMsgRefresh = 'amrr';
const uint32 kMsgOpenLink = 'amol';
const uint32 kMsgMarkRead = 'ammr';

TWindow::TWindow(bool smokeTest)
	: BWindow(BRect(80, 80, 900, 620), "Anchorman", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSmokeTest(smokeTest),
	fFeedListener(NULL),
	fChannelList(NULL),
	fItemList(NULL),
	fContentView(NULL),
	fOpenButton(NULL),
	fMarkReadButton(NULL)
{
	BuildInterface();
	if (!fSmokeTest)
		StartListener();
}

TWindow::~TWindow()
{
	StopListener();
	ClearFeeds();
}

bool
TWindow::QuitRequested()
{
	StopListener();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void
TWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgReloadFeeds:
			ReloadFeeds();
			break;
		case kMsgChannelSelected:
			ReloadItems();
			break;
		case kMsgItemSelected:
			DisplayItem();
			break;
		case kMsgRefresh:
			if (fFeedListener != NULL)
				fFeedListener->RefreshAllFeeds();
			break;
		case kMsgOpenLink:
			OpenSelectedLink();
			break;
		case kMsgMarkRead: {
			int32 selection = fItemList->CurrentSelection();
			if (fFeedListener != NULL && selection >= 0
				&& (size_t)selection < fItems.size())
				fFeedListener->MarkItemRead(fItems[selection]);
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}

void
TWindow::ServerStarted()
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::ServerShutdown()
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::FeedRegistered(Feed */*feed*/)
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::FeedSubscriptionChanged(Feed */*feed*/)
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::ChannelUpdated(Feed */*feed*/, Channel */*channel*/)
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::ItemRead(Feed */*feed*/, Channel */*channel*/, Item */*item*/)
{
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::BuildInterface()
{
	BRect bounds = Bounds();
	BView *background = new BView(bounds, "background", B_FOLLOW_ALL,
		B_WILL_DRAW);
	background->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	AddChild(background);

	const float margin = 10;
	const float buttonHeight = 28;
	const float leftWidth = 220;
	const float middleWidth = 260;
	BRect listRect(margin, margin, leftWidth, bounds.bottom - margin);
	fChannelList = new BListView(listRect, "channels", B_SINGLE_SELECTION_LIST,
		B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM);
	fChannelList->SetSelectionMessage(new BMessage(kMsgChannelSelected));
	background->AddChild(new BScrollView("channel scroll", fChannelList,
		B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM, 0, false, true));

	listRect.left = leftWidth + margin * 2;
	listRect.right = listRect.left + middleWidth;
	fItemList = new BListView(listRect, "items", B_SINGLE_SELECTION_LIST,
		B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM);
	fItemList->SetSelectionMessage(new BMessage(kMsgItemSelected));
	background->AddChild(new BScrollView("item scroll", fItemList,
		B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM, 0, false, true));

	BRect contentRect(listRect.right + margin * 2, margin,
		bounds.right - margin, bounds.bottom - buttonHeight - margin * 2);
	BRect textRect = contentRect;
	textRect.OffsetTo(B_ORIGIN);
	textRect.InsetBy(5, 5);
	fContentView = new BTextView(contentRect, "content", textRect,
		B_FOLLOW_ALL, B_WILL_DRAW | B_NAVIGABLE);
	fContentView->MakeEditable(false);
	fContentView->SetText("Select a feed item to read it.");
	background->AddChild(new BScrollView("content scroll", fContentView,
		B_FOLLOW_ALL, 0, true, true));

	BRect buttonRect(contentRect.left, contentRect.bottom + margin,
		contentRect.left + 110, contentRect.bottom + margin + buttonHeight);
	BButton *refresh = new BButton(buttonRect, "refresh", "Refresh",
		new BMessage(kMsgRefresh), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	background->AddChild(refresh);
	buttonRect.OffsetBy(120, 0);
	fMarkReadButton = new BButton(buttonRect, "mark read", "Mark read",
		new BMessage(kMsgMarkRead), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	fMarkReadButton->SetEnabled(false);
	background->AddChild(fMarkReadButton);
	buttonRect.OffsetBy(120, 0);
	fOpenButton = new BButton(buttonRect, "open", "Open link",
		new BMessage(kMsgOpenLink), B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
	fOpenButton->SetEnabled(false);
	background->AddChild(fOpenButton);
}

void
TWindow::StartListener()
{
	fFeedListener = new FeedListener();
	fFeedListener->AddHandler(this);
	fFeedListener->StartListening();
	PostMessage(kMsgReloadFeeds);
}

void
TWindow::StopListener()
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

void
TWindow::ClearFeeds()
{
	for (feed_list_t::iterator iterator = fFeeds.begin();
		iterator != fFeeds.end(); iterator++)
		delete *iterator;
	fFeeds.clear();
	fChannels.clear();
	fItems.clear();
}

void
TWindow::ReloadFeeds()
{
	fChannelList->MakeEmpty();
	fItemList->MakeEmpty();
	fContentView->SetText("Select a feed item to read it.");
	fSelectedLink = "";
	fOpenButton->SetEnabled(false);
	fMarkReadButton->SetEnabled(false);
	ClearFeeds();

	if (fFeedListener == NULL || !fFeedListener->ServerIsRunning())
		return;
	fFeeds = fFeedListener->Feeds();
	for (feed_list_t::iterator feed = fFeeds.begin(); feed != fFeeds.end(); feed++) {
		for (uint32 index = 0; index < (*feed)->ChannelCount(); index++) {
			Channel *channel = (*feed)->ChannelAt(index);
			fChannels.push_back(channel);
			BString label((*feed)->DisplayName());
			if (label.Length() > 0)
				label << " — ";
			label << channel->Title();
			fChannelList->AddItem(new BStringItem(label.String()));
		}
	}
}

void
TWindow::ReloadItems()
{
	fItemList->MakeEmpty();
	fItems.clear();
	fContentView->SetText("Select a feed item to read it.");
	fSelectedLink = "";
	fOpenButton->SetEnabled(false);
	fMarkReadButton->SetEnabled(false);

	int32 selection = fChannelList->CurrentSelection();
	if (selection < 0 || (size_t)selection >= fChannels.size())
		return;
	Channel *channel = fChannels[selection];
	for (uint32 index = 0; index < channel->ItemCount(); index++) {
		Item *item = channel->ItemAt(index);
		fItems.push_back(item);
		fItemList->AddItem(new BStringItem(item->Title()));
	}
}

void
TWindow::DisplayItem()
{
	int32 selection = fItemList->CurrentSelection();
	if (selection < 0 || (size_t)selection >= fItems.size())
		return;
	Item *item = fItems[selection];

	BString content(item->Title());
	content << "\n\n";
	if (item->ContentCount() > 0)
		content << item->ContentAt(0)->Text();
	fContentView->SetText(content.String());
	fSelectedLink = item->Link();
	fOpenButton->SetEnabled(fSelectedLink.Length() > 0);
	fMarkReadButton->SetEnabled(!item->Read());
}

void
TWindow::OpenSelectedLink()
{
	if (fSelectedLink.Length() == 0)
		return;
	const char *arguments[] = { fSelectedLink.String() };
	be_roster->Launch("text/html", 1, arguments);
}
