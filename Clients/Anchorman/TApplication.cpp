#include "TApplication.h"

#include "Settings.h"
#include "TWindow.h"

#include <stdio.h>

TApplication::TApplication(bool smokeTest)
	: BApplication(kAppSignature),
	fSmokeTest(smokeTest),
	fWindow(NULL)
{
}

void
TApplication::ReadyToRun()
{
	fWindow = new TWindow(fSmokeTest);
	fWindow->Show();
	if (fSmokeTest) {
		printf("Anchorman smoke test passed\n");
		fWindow->PostMessage(B_QUIT_REQUESTED);
	}
}
