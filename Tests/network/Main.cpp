#include "FileRequest.h"
#include "FileRequestHandler.h"

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <String.h>
#include <OS.h>

#include <curl/curl.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>

static int32 sChecks = 0;
static int32 sFailures = 0;

class CancellingHandler : public FileRequestHandler {
	public:
		void DownloadProgress(FileRequest *request, void *, double, double)
		{
			request->Cancel();
		}
};

class ClosingStorageHandler : public FileRequestHandler {
	public:
					ClosingStorageHandler()
						: fReleaseStatus(B_NO_INIT) {
					}

		void		DownloadStarted(FileRequest *request, void *) {
						fReleaseStatus = request->ReleaseTemporaryStorage();
					}

		status_t	ReleaseStatus(void) const {
						return fReleaseStatus;
					}

	private:
		status_t	fReleaseStatus;
};

class LocalHTTPServer {
	public:
					LocalHTTPServer(const char *body, size_t bodySize)
						: fBody(body),
						fBodySize(bodySize),
						fSocket(-1),
						fThread(-1),
						fPort(0),
						fRunning(false) {
					}

		bool		Start(void) {
						fSocket = socket(AF_INET, SOCK_STREAM, 0);
						if (fSocket < 0)
							return false;

						int reuseAddress = 1;
						setsockopt(fSocket, SOL_SOCKET, SO_REUSEADDR,
							&reuseAddress, sizeof(reuseAddress));

						struct sockaddr_in address;
						memset(&address, 0, sizeof(address));
						address.sin_family = AF_INET;
						address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
						address.sin_port = 0;
						if (bind(fSocket, reinterpret_cast<struct sockaddr *>(&address),
								sizeof(address)) != 0
							|| listen(fSocket, 4) != 0) {
							close(fSocket);
							fSocket = -1;
							return false;
						}

						socklen_t addressSize = sizeof(address);
						if (getsockname(fSocket,
								reinterpret_cast<struct sockaddr *>(&address),
								&addressSize) != 0) {
							close(fSocket);
							fSocket = -1;
							return false;
						}
						fPort = ntohs(address.sin_port);

						fRunning.store(true);
						fThread = spawn_thread(Run, "FeedKit HTTP test server",
							B_NORMAL_PRIORITY, this);
						if (fThread < B_OK || resume_thread(fThread) != B_OK) {
							fRunning.store(false);
							close(fSocket);
							fSocket = -1;
							fThread = -1;
							return false;
						}
						return true;
					}

		bool		Stop(void) {
						fRunning.store(false);
						status_t threadResult = B_OK;
						if (fThread >= B_OK)
							wait_for_thread(fThread, &threadResult);
						if (fSocket >= 0)
							close(fSocket);
						fSocket = -1;
						fThread = -1;
						return threadResult == B_OK;
					}

		uint16		Port(void) const {
						return fPort;
					}

	private:
		static int32 Run(void *data) {
						return static_cast<LocalHTTPServer *>(data)->Serve();
					}

		int32		Serve(void) {
						while (fRunning.load()) {
							fd_set sockets;
							FD_ZERO(&sockets);
							FD_SET(fSocket, &sockets);
							struct timeval timeout = { 0, 100000 };
							int ready = select(fSocket + 1, &sockets, NULL, NULL,
								&timeout);
							if (ready < 0) {
								if (errno == EINTR)
									continue;
								return B_ERROR;
							}
							if (ready == 0)
								continue;

							int client = accept(fSocket, NULL, NULL);
							if (client < 0) {
								if (errno == EINTR)
									continue;
								return B_ERROR;
							}
							ServeRequest(client);
							close(client);
						}
						return B_OK;
					}

		void		ServeRequest(int client) {
						char request[2048];
						size_t used = 0;
						while (used < sizeof(request) - 1) {
							ssize_t received = recv(client, request + used,
								sizeof(request) - 1 - used, 0);
							if (received < 0 && errno == EINTR)
								continue;
							if (received <= 0)
								break;
							used += received;
							request[used] = '\0';
							if (strstr(request, "\r\n") != NULL)
								break;
						}
						if (used == 0)
							return;
						request[used] = '\0';

						if (strncmp(request, "GET /write-failure ", 19) == 0) {
							char header[256];
							int length = snprintf(header, sizeof(header),
								"HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n"
								"Connection: close\r\n\r\n", (unsigned long)fBodySize);
							if (length > 0 && static_cast<size_t>(length) < sizeof(header)
									&& fBodySize > 1) {
								Send(client, header, length);
								Send(client, fBody, 1);
								snooze(100000);
								Send(client, fBody + 1, fBodySize - 1);
							}
							return;
						}

						if (strncmp(request, "GET /redirect ", 14) == 0) {
							const char response[] = "HTTP/1.1 302 Found\r\n"
								"Location: /feed\r\nContent-Length: 0\r\n"
								"Connection: close\r\n\r\n";
							Send(client, response, sizeof(response) - 1);
							return;
						}

						if (strncmp(request, "GET /feed ", 10) == 0) {
							char header[256];
							int length = snprintf(header, sizeof(header),
								"HTTP/1.1 200 OK\r\n"
								"Content-Type: application/rss+xml\r\n"
								"Content-Length: %lu\r\nConnection: close\r\n\r\n",
								(unsigned long)fBodySize);
							if (length > 0 && static_cast<size_t>(length) < sizeof(header)) {
								Send(client, header, length);
								Send(client, fBody, fBodySize);
							}
							return;
						}

						const char response[] = "HTTP/1.1 404 Not Found\r\n"
							"Content-Length: 0\r\nConnection: close\r\n\r\n";
						Send(client, response, sizeof(response) - 1);
					}

		static bool Send(int socket, const void *buffer, size_t size) {
						const char *data = static_cast<const char *>(buffer);
						size_t sent = 0;
						while (sent < size) {
							ssize_t result = send(socket, data + sent, size - sent, 0);
							if (result < 0 && errno == EINTR)
								continue;
							if (result <= 0)
								return false;
							sent += result;
						}
						return true;
					}

		const char		*fBody;
		size_t			fBodySize;
		int				fSocket;
		thread_id		fThread;
		uint16			fPort;
		std::atomic<bool>	fRunning;
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

	setenv("NO_PROXY", "127.0.0.1,localhost", 1);
	LocalHTTPServer server(fixtureData, fixtureSize);
	bool serverStarted = fixtureData != NULL && server.Start();
	Check(serverStarted, "Could not start the local HTTP fixture server");
	if (serverStarted) {
		BString baseURL("http://127.0.0.1:");
		baseURL << static_cast<int32>(server.Port());

		BString feedURL(baseURL);
		feedURL << "/feed";
		FileRequest httpRequest(feedURL.String());
		Check(httpRequest.Start() == B_OK,
			"Local HTTP request did not complete");
		Check(httpRequest.CurlStatus() == CURLE_OK,
			"Local HTTP request retained a curl error");
		Check(httpRequest.HTTPStatus() == 200,
			"Local HTTP request retained the wrong status");
		Check(strcmp(httpRequest.MIME(), "application/rss+xml") == 0,
			"Local HTTP request did not retain its content type");
		Check(httpRequest.Size() == static_cast<int32>(fixtureSize),
			"Local HTTP request reported the wrong size");
		char *httpData = new char[fixtureSize == 0 ? 1 : fixtureSize];
		Check(httpRequest.Read(httpData, fixtureSize)
			== static_cast<ssize_t>(fixtureSize),
			"Local HTTP response could not be read back");
		Check(memcmp(httpData, fixtureData, fixtureSize) == 0,
			"Local HTTP response content changed");
		delete[] httpData;

		BString redirectURL(baseURL);
		redirectURL << "/redirect";
		FileRequest redirectRequest(redirectURL.String());
		Check(redirectRequest.Start() == B_OK,
			"Local HTTP redirect did not complete");
		Check(redirectRequest.CurlStatus() == CURLE_OK,
			"Local HTTP redirect retained a curl error");
		Check(redirectRequest.HTTPStatus() == 200,
			"Local HTTP redirect did not retain the final status");
		Check(redirectRequest.Size() == static_cast<int32>(fixtureSize),
			"Local HTTP redirect reported the wrong size");
		char *redirectData = new char[fixtureSize == 0 ? 1 : fixtureSize];
		Check(redirectRequest.Read(redirectData, fixtureSize)
			== static_cast<ssize_t>(fixtureSize),
			"Local HTTP redirect response could not be read back");
		Check(memcmp(redirectData, fixtureData, fixtureSize) == 0,
			"Local HTTP redirect response content changed");
		delete[] redirectData;

		BString missingHTTPURL(baseURL);
		missingHTTPURL << "/missing";
		FileRequest missingHTTPRequest(missingHTTPURL.String());
		Check(missingHTTPRequest.Start() != B_OK,
			"HTTP 404 response unexpectedly succeeded");
		Check(missingHTTPRequest.CurlStatus() == CURLE_HTTP_RETURNED_ERROR,
			"HTTP 404 response retained the wrong curl status");
		Check(missingHTTPRequest.HTTPStatus() == 404,
			"HTTP 404 response did not retain its HTTP status");
		Check(missingHTTPRequest.Error() != NULL
			&& strlen(missingHTTPRequest.Error()) > 0,
			"HTTP 404 response did not retain an error description");

		BString writeFailureURL(baseURL);
		writeFailureURL << "/write-failure";
		FileRequest writeFailureRequest(writeFailureURL.String());
		ClosingStorageHandler closingHandler;
		writeFailureRequest.AddHandler(&closingHandler, NULL, false);
		status_t writeFailureStatus = writeFailureRequest.Start();
		Check(writeFailureStatus != B_OK,
			"Storage write failure unexpectedly succeeded");
		Check(writeFailureRequest.CurlStatus() == CURLE_WRITE_ERROR,
			"Storage write failure retained the wrong curl status");
		Check(closingHandler.ReleaseStatus() == B_OK,
			"Storage write failure fixture did not close its output");
		Check(writeFailureRequest.StorageStatus() != B_OK,
			"Storage write failure did not retain its Haiku status");
		Check(writeFailureStatus == writeFailureRequest.StorageStatus(),
			"Storage write failure did not return its Haiku status");
		Check(writeFailureRequest.Completed(),
			"Storage write failure was not marked complete");
		Check(writeFailureRequest.Size() > 0
			&& writeFailureRequest.Size() < static_cast<int32>(fixtureSize),
			"Storage write failure reported an invalid partial size");
		Check(writeFailureRequest.Error() != NULL
			&& strlen(writeFailureRequest.Error()) > 0,
			"Storage write failure did not retain an error description");

		Check(server.Stop(), "Local HTTP fixture server did not stop cleanly");
	}

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
