#include "FileRequest.h"
#include "FileRequestHandler.h"

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int32 sChecks = 0;
static int32 sFailures = 0;

class CancellingHandler : public FileRequestHandler {
	public:
		void DownloadProgress(FileRequest *request, void *, double, double)
		{
			request->Cancel();
		}
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

static int
CreateTemporaryFile(BString *path)
{
	BPath temporaryDirectory;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &temporaryDirectory) != B_OK)
		return -1;
	if (temporaryDirectory.Append("feedkit-network-test-XXXXXX") != B_OK)
		return -1;

	char *pathTemplate = strdup(temporaryDirectory.Path());
	if (pathTemplate == NULL)
		return -1;
	int descriptor = mkstemp(pathTemplate);
	if (descriptor >= 0)
		*path = pathTemplate;
	free(pathTemplate);
	return descriptor;
}

static bool
ReadFile(const char *path, char **buffer, size_t *size)
{
	BFile file(path, B_READ_ONLY);
	off_t fileSize = 0;
	if (file.InitCheck() != B_OK || file.GetSize(&fileSize) != B_OK
		|| fileSize < 0)
		return false;

	char *data = new char[fileSize == 0 ? 1 : fileSize];
	ssize_t bytesRead = file.Read(data, fileSize);
	if (bytesRead != fileSize) {
		delete[] data;
		return false;
	}

	*buffer = data;
	*size = static_cast<size_t>(fileSize);
	return true;
}

static void
RunTests(const char *fixturePath)
{
	char *fixtureData = NULL;
	size_t fixtureSize = 0;
	Check(ReadFile(fixturePath, &fixtureData, &fixtureSize),
		"Could not read the network test fixture");

	BString fixtureURL("file://");
	fixtureURL << fixturePath;
	FileRequest temporaryRequest(fixtureURL.String());
	Check(temporaryRequest.StorageStatus() == B_OK,
		"Temporary request storage was not initialized");
	Check(temporaryRequest.Start() == B_OK,
		"Local file request did not complete");
	Check(temporaryRequest.CurlStatus() == CURLE_OK,
		"Successful request retained a curl error");
	Check(temporaryRequest.Completed(),
		"Successful request was not marked complete");
	Check(temporaryRequest.Size() == static_cast<int32>(fixtureSize),
		"Successful request reported the wrong size");
	char *downloaded = new char[fixtureSize == 0 ? 1 : fixtureSize];
	Check(temporaryRequest.Read(downloaded, fixtureSize)
		== static_cast<ssize_t>(fixtureSize),
		"Successful request could not be read back");
	Check(fixtureData != NULL
		&& memcmp(downloaded, fixtureData, fixtureSize) == 0,
		"Downloaded fixture content changed");
	delete[] downloaded;
	Check(temporaryRequest.ReleaseTemporaryStorage() == B_OK,
		"Temporary request storage was not released");

	BString missingURL(fixtureURL);
	missingURL << ".missing";
	FileRequest missingRequest(missingURL.String());
	Check(missingRequest.Start() != B_OK,
		"Missing local file unexpectedly downloaded");
	Check(missingRequest.CurlStatus() != CURLE_OK,
		"Missing local file did not retain its curl status");
	Check(missingRequest.Error() != NULL
		&& strlen(missingRequest.Error()) > 0,
		"Missing local file did not retain an error description");

	FileRequest cancelledRequest(fixtureURL.String());
	cancelledRequest.AddHandler(new CancellingHandler(), NULL);
	Check(cancelledRequest.Start() == DownloadCancelled,
		"Cancelled request returned the wrong status");
	Check(cancelledRequest.CurlStatus() == CURLE_ABORTED_BY_CALLBACK,
		"Cancelled request did not retain its curl status");
	Check(cancelledRequest.Completed(),
		"Cancelled request was not marked complete");

	BString outputPath;
	int outputDescriptor = CreateTemporaryFile(&outputPath);
	Check(outputDescriptor >= 0, "Could not create the output fixture");
	if (outputDescriptor >= 0) {
		const char stale[] = "stale data that must be truncated";
		write(outputDescriptor, stale, sizeof(stale));
		close(outputDescriptor);

		entry_ref outputRef;
		status_t refStatus = get_ref_for_path(outputPath.String(), &outputRef);
		Check(refStatus == B_OK,
			"Could not resolve the output fixture");
		if (refStatus == B_OK) {
			FileRequest outputRequest(fixtureURL.String(), outputRef, BMessage());
			Check(outputRequest.Start() == B_OK,
				"File-backed request did not complete");
			BFile output(outputPath.String(), B_READ_ONLY);
			off_t outputSize = -1;
			Check(output.GetSize(&outputSize) == B_OK
				&& outputSize == static_cast<off_t>(fixtureSize),
				"Fresh file-backed request did not truncate stale data");
		}
		unlink(outputPath.String());
	}

	BString resumePath;
	int resumeDescriptor = CreateTemporaryFile(&resumePath);
	Check(resumeDescriptor >= 0, "Could not create the resume fixture");
	if (resumeDescriptor >= 0 && fixtureData != NULL) {
		size_t prefixSize = fixtureSize < 32 ? fixtureSize : 32;
		Check(write(resumeDescriptor, fixtureData, prefixSize)
			== static_cast<ssize_t>(prefixSize),
			"Could not prepare the resume fixture");
		close(resumeDescriptor);

		entry_ref resumeRef;
		status_t refStatus = get_ref_for_path(resumePath.String(), &resumeRef);
		Check(refStatus == B_OK,
			"Could not resolve the resume fixture");
		if (refStatus == B_OK) {
			FileRequest resumeRequest(fixtureURL.String(), resumeRef, BMessage(),
				static_cast<int32>(prefixSize));
			Check(resumeRequest.Start() == B_OK,
				"Resumed file-backed request did not complete");
			Check(resumeRequest.Size() == static_cast<int32>(fixtureSize),
				"Resumed request reported only the transferred suffix");
			char *resumed = new char[fixtureSize == 0 ? 1 : fixtureSize];
			Check(resumeRequest.Read(resumed, fixtureSize)
				== static_cast<ssize_t>(fixtureSize),
				"Resumed request could not be read back");
			Check(memcmp(resumed, fixtureData, fixtureSize) == 0,
				"Resumed request produced corrupt content");
			delete[] resumed;
		}
		unlink(resumePath.String());
	} else if (resumeDescriptor >= 0) {
		close(resumeDescriptor);
		unlink(resumePath.String());
	}

	FileRequest unsupported("gopher://example.invalid/feed");
	Check(unsupported.Start() != B_OK,
		"Unsupported protocol unexpectedly succeeded");
	Check(unsupported.CurlStatus() == CURLE_UNSUPPORTED_PROTOCOL,
		"Unsupported protocol returned the wrong curl status");

	delete[] fixtureData;
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s FIXTURE\n", argv[0]);
		return 1;
	}

	CURLcode globalStatus = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (globalStatus != CURLE_OK) {
		fprintf(stderr, "curl initialization failed: %s\n",
			curl_easy_strerror(globalStatus));
		return 1;
	}

	RunTests(argv[1]);
	curl_global_cleanup();

	if (sFailures == 0) {
		printf("FeedKit FileRequest tests passed: %d checks.\n",
			(int)sChecks);
		return 0;
	}

	fprintf(stderr, "FeedKit FileRequest tests failed: %d of %d checks failed.\n",
		(int)sFailures, (int)sChecks);
	return 1;
}
