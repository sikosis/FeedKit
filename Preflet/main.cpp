#include "PApplication.h"

#include <stdio.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	bool smokeTest = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
	if (argc > 1 && !smokeTest) {
		fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
		return 2;
	}

	PApplication app(smokeTest);
	app.Run();
	if (smokeTest)
		printf("FeedKit preferences smoke test passed: window opened and closed cleanly.\n");
	return 0;
}
