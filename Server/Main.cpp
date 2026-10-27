#include "FeedServer.h"

#include <stdio.h>
#include <string.h>

#if 0
#include <Message.h>
#include <String.h>
#include <map>

#include <Flattenable.h>

class FlatTest : public BFlattenable {
	public:
	int32 value;

	// BFlattenable Hooks
	status_t			Flatten(void *buffer, ssize_t numBytes) const {
		BMessage msg;
		msg.AddInt32("value", value);

printf("Value is %i\n", value);

		return msg.Flatten((char *)buffer, numBytes);
	};
	
	status_t			Unflatten(type_code code, const void *buffer, ssize_t numBytes) {
		BMessage msg;
		status_t result = msg.Unflatten((char *)buffer);
		
		if (result == B_OK) {
			if (msg.FindInt32("value", &value) != B_OK) result = B_ERROR;
		};
		
		return result;
	};
	
	ssize_t				FlattenedSize(void) const {
		BMessage msg;
		msg.AddInt32("value", value);
		
		return msg.FlattenedSize();
	};

	bool				IsFixedSize(void) const {
		return true;
	};
	type_code			TypeCode(void) const {
		return 'flat';
	};
	
	bool				AllowsTypeCode(type_code code) const {
		return code == 'flat';
	};

	
};

class DelayMessage : public BMessage {
	public:
		status_t AddDelayedFlat(const char *name, BFlattenable *flat) {
			fFlat[name] = flat;
			
			return B_OK;
		};

		status_t AddFlat(const char *name, BFlattenable *flat) {
			return AddDelayedFlat(name, flat);
		};

		operator BMessage *() {
			BMessage *msg = this;
			
			map<BString, BFlattenable *>::iterator iIt;
			for (iIt = fFlat.begin(); iIt != fFlat.end(); iIt++) {
				msg->AddFlat(iIt->first.String(), (iIt->second));
			};
			
			return msg;
		}
		
	private:
		map<BString, BFlattenable *> fFlat;
	
};

#include "ObjectUpdateFilter.h"
#endif

int main(int argc, char **argv) {
	bool smokeTest = argc == 2 && strcmp(argv[1], "--smoke-test") == 0;
	bool integrationTest = (argc >= 4 && argc <= 6)
		&& strcmp(argv[1], "--integration-test") == 0;
	bool clientTest = argc == 3 && strcmp(argv[1], "--client-test") == 0;
	if (argc > 1 && !smokeTest && !integrationTest && !clientTest) {
		fprintf(stderr, "Usage: %s [--smoke-test | --client-test STORE | --integration-test RSS_URL ATOM_URL [DOWNLOAD_FAILURE_URL [PARSE_FAILURE_URL]]]\n",
			argv[0]);
		return 1;
	}

	FeedServer server(smokeTest, integrationTest ? argv[2] : NULL,
		integrationTest ? argv[3] : NULL,
		integrationTest && argc >= 5 ? argv[4] : NULL,
		integrationTest && argc == 6 ? argv[5] : NULL,
		clientTest ? argv[2] : NULL);
	status_t status = server.InitCheck();
	if (status != B_OK) {
		fprintf(stderr, "feed_server initialization failed: %s\n",
			strerror(status));
		return 1;
	}
	if (smokeTest)
		fprintf(stderr, "FeedServer smoke test: constructor complete\n");

	server.Run();
	
	return integrationTest && !server.IntegrationTestPassed() ? 1 : 0;
	
#if 0
//	ObjectUpdateFilter filter(&server);
//	FlatTest *test = new FlatTest();
//	test->value = 0;
//
//	DelayedFlattenedObject progressUpdateCommand("flat", test);
//	BMessage progress;
//	progress.AddFlat("delayedupdate", &progressUpdateCommand);
//
//	test->value = 0xffffffff;
//	
//	progress.PrintToStream();
//	
//	printf("Flattening...\n");
//	
//	bool flat = filter.FlattenDelayedObjects(&progress);
//	printf("Has flat: %s\n", flat == true ? "yup" : "nope");


//	FlatTest *test = new FlatTest();
//	BMessage msg;
//	msg.AddMessage
	
//	server.Run();

//	DelayMessage msg;
//	FlatTest *test = new FlatTest();
//	test->value = 0;
	
//	msg.AddDelayedFlat("blah", test);
//	msg.AddFlat("blah", test);	
//	test->value = 100;
	
//	msg.PrintToStream();
//
//	BMessage *bmsg = msg;
//	
//	bmsg->PrintToStream();
//
//	FlatTest two;
//	bmsg->FindFlat("blah", &two);
//	
//	printf("Value: %i\n", two.value);
#endif
};
