#include <libfeedkit/FeedKitConstants.h>
#include <libfeedkit/FeedListener.h>

#include <Application.h>
#include <Entry.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>

using namespace FeedKit;

static bool
LoadedImageContains(team_id team, const char *pathPart)
{
	int32 cookie = 0;
	image_info image;
	while (get_next_image_info(team, &cookie, &image) == B_OK) {
		if (strstr(image.name, pathPart) != NULL)
			return true;
	}
	return false;
}

static bool
WaitForServer(bool running, app_info *info)
{
	for (int32 attempt = 0; attempt < 100; attempt++) {
		app_info current;
		bool found = be_roster->GetAppInfo(ServerSignature, &current) == B_OK;
		if (found == running) {
			if (found && info != NULL)
				*info = current;
			return true;
		}
		snooze(100000);
	}
	return false;
}

class LifecycleApplication : public BApplication {
	public:
		LifecycleApplication()
			: BApplication("application/x-vnd.feedkit-package-lifecycle-test"),
			fResult(1)
		{
		}

		int Result() const
		{
			return fResult;
		}

		void ReadyToRun()
		{
			if (!LoadedImageContains(B_CURRENT_TEAM,
					"/system/lib/libfeedkit.so")) {
				fprintf(stderr, "FAIL: libfeedkit.so was not loaded from the packaged system library directory\n");
				PostMessage(B_QUIT_REQUESTED);
				return;
			}

			BMessenger existing(ServerSignature);
			if (existing.IsValid()) {
				existing.SendMessage(B_QUIT_REQUESTED);
				if (!WaitForServer(false, NULL)) {
					fprintf(stderr, "FAIL: an existing feed_server did not stop\n");
					PostMessage(B_QUIT_REQUESTED);
					return;
				}
			}

			FeedListener *listener = new FeedListener();
			listener->StartListening();

			app_info serverInfo;
			if (!WaitForServer(true, &serverInfo)) {
				fprintf(stderr, "FAIL: StartListening did not launch the packaged feed_server\n");
				delete listener;
				PostMessage(B_QUIT_REQUESTED);
				return;
			}

			BPath serverPath(&serverInfo.ref);
			if (serverPath.InitCheck() != B_OK
				|| strstr(serverPath.Path(), "/system/servers/feed_server") == NULL) {
				fprintf(stderr, "FAIL: feed_server was not launched from the packaged system server directory: %s\n",
					serverPath.Path() != NULL ? serverPath.Path() : "<unknown>");
				StopServer(serverInfo.team);
				delete listener;
				PostMessage(B_QUIT_REQUESTED);
				return;
			}

			bool atomLoaded = false;
			bool rssLoaded = false;
			for (int32 attempt = 0; attempt < 100 && (!atomLoaded || !rssLoaded);
					attempt++) {
				atomLoaded = LoadedImageContains(serverInfo.team,
					"/system/add-ons/feed_kit/Parsers/Atom");
				rssLoaded = LoadedImageContains(serverInfo.team,
					"/system/add-ons/feed_kit/Parsers/RSS");
				if (!atomLoaded || !rssLoaded)
					snooze(100000);
			}

			if (!atomLoaded || !rssLoaded) {
				fprintf(stderr, "FAIL: packaged parser add-ons were not loaded\n");
				StopServer(serverInfo.team);
				delete listener;
				PostMessage(B_QUIT_REQUESTED);
				return;
			}

			StopServer(serverInfo.team);
			delete listener;
			fResult = 0;
			printf("FeedKit installed-package test passed: packaged library loaded, StartListening launched the packaged server, and Atom/RSS add-ons loaded.\n");
			PostMessage(B_QUIT_REQUESTED);
		}

	private:
		void StopServer(team_id)
		{
			BMessenger server(ServerSignature);
			if (server.IsValid())
				server.SendMessage(B_QUIT_REQUESTED);
			WaitForServer(false, NULL);
		}

		int fResult;
};

int
main()
{
	LifecycleApplication application;
	application.Run();
	return application.Result();
}
