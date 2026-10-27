#include "PApplication.h"
#include "PWindow.h"

//#pragma mark Constructor

PApplication::PApplication(bool smokeTest)
	: BApplication("application/x-vnd.beclan-FeedKit-Prefs"),
	fWindow(NULL),
	fSmokeTest(smokeTest) {
};

PApplication::~PApplication(void) {
};

//#pragma mark BApplicaiton Hooks

void PApplication::ReadyToRun(void) {
	fWindow = new PWindow();
	if (fSmokeTest)
		fWindow->PostMessage(B_QUIT_REQUESTED);
};

bool PApplication::QuitRequested(void) {
	return true;
};

void PApplication::MessageReceived(BMessage *msg) {
	BApplication::MessageReceived(msg);
};

