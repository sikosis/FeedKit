#include <Application.h>
#include <Deskbar.h>
#include <Path.h>
#include <Roster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Constants.h"
#include "FeedIcon.h"

static int
RunSmokeTest()
{
	BMessage archive;
	FeedIcon original;
	status_t status = original.Archive(&archive);
	if (status != B_OK) {
		fprintf(stderr, "Unable to archive the Deskbar view: %s\n",
			strerror(status));
		return 1;
	}

	BArchivable *copy = FeedIcon::Instantiate(&archive);
	if (copy == NULL) {
		fprintf(stderr, "Unable to restore the archived Deskbar view\n");
		return 1;
	}
	delete copy;

	printf("FeedKit Deskbar smoke test passed\n");
	return 0;
}

int main(int argc, char *argv[]) {
	BApplication application(APP_SIG);

	if (argc == 2 && strcmp(argv[1], "--smoke-test") == 0)
		return RunSmokeTest();
	if (argc > 1) {
		fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
		return 2;
	}

	BDeskbar deskbar;
	app_info info;

	// Remove the old instance
	if (deskbar.HasItem(VIEW_NAME) == true) deskbar.RemoveItem(VIEW_NAME);

	if (application.GetAppInfo(&info) != B_OK) {
		fprintf(stderr, "Unable to obtain app info\n");
		return 1;
	}

	status_t result = deskbar.AddItem(&info.ref, NULL);
	printf("Adding deskbar icon (%s): %s\n", info.ref.name, strerror(result));
	return result == B_OK ? 0 : 1;
};
