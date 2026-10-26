#include "FileRequest.h"
#include "FileRequestHandler.h"

#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <curl/curlver.h>
#include <curl/curl.h>
#include <curl/easy.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <limits>

//#pragma mark RequestHandlerInfo

class RequestHandlerInfo {
	public:
							RequestHandlerInfo(FileRequestHandler *handler, void *data,
								bool takeOwnership)
								: fHandler(handler),
								fData(data),
								fTakeOwnership(takeOwnership) {
							};
							~RequestHandlerInfo(void) {
								if (fTakeOwnership)
									delete fHandler;
							};
		void				DownloadStarted(FileRequest *request) {
								fHandler->DownloadStarted(request, fData);
							};
		void				DownloadProgress(FileRequest *request, double downTotal,
								double downCurrent) {
								
								fHandler->DownloadProgress(request, fData, downTotal,
									downCurrent);
							};
	
	private:
		FileRequestHandler	*fHandler;
		void				*fData;
		bool				fTakeOwnership;
};


//#pragma mark Constants

const int32 kMaxMallocIOSize = 1024 * 1024 * 10;	// 10 MB

//#pragma mark Constructor

FileRequest::FileRequest(const char *url, entry_ref ref, BMessage progress, int32 offset)
	: fURL(url),
	fRef(ref),
	fTemporaryFD(-1),
	fStorageStatus(B_NO_INIT),
	fCurl(NULL),
	fCurlStatus(CURLE_OK),
	fResult(CURLE_OK),
	fHTTPStatus(0),
	fProgress(progress),
	fOffset(offset < 0 ? 0 : offset) {

	uint32 openMode = B_READ_WRITE | B_CREATE_FILE;
	if (fOffset == 0)
		openMode |= B_ERASE_FILE;
	fStorageStatus = fFile.SetTo(&ref, openMode);
	if (fStorageStatus == B_OK && fOffset > 0
		&& fFile.Seek(fOffset, SEEK_SET) != fOffset)
		fStorageStatus = B_FILE_ERROR;

	Init();
};

FileRequest::FileRequest(const char *url, BMessage progress, int32 offset)
	: fURL(url),
	fTemporaryFD(-1),
	fStorageStatus(B_NO_INIT),
	fCurl(NULL),
	fCurlStatus(CURLE_OK),
	fResult(CURLE_OK),
	fHTTPStatus(0),
	fProgress(progress),
	fOffset(0) {
	(void)offset;

	BPath path;
	fStorageStatus = find_directory(B_SYSTEM_TEMP_DIRECTORY, &path);
	if (fStorageStatus == B_OK)
		fStorageStatus = path.Append("feedkit-XXXXXX");

	if (fStorageStatus == B_OK) {
		char *pathTemplate = strdup(path.Path());
		if (pathTemplate == NULL) {
			fStorageStatus = B_NO_MEMORY;
		} else {
			fTemporaryFD = mkstemp(pathTemplate);
			if (fTemporaryFD < 0) {
				fStorageStatus = B_FILE_ERROR;
			} else if (::unlink(pathTemplate) != 0) {
				::close(fTemporaryFD);
				fTemporaryFD = -1;
				fStorageStatus = B_FILE_ERROR;
			} else {
				fStorageStatus = B_OK;
			}
			free(pathTemplate);
		}
	}
	
	Init();
};

FileRequest::~FileRequest(void) {
	if (fCurl != NULL)
		curl_easy_cleanup(fCurl);
	if (fTemporaryFD >= 0)
		::close(fTemporaryFD);
	
	for (handlerinfo_t::iterator rIt = fHandler.begin(); rIt != fHandler.end(); rIt++) {
		delete (*rIt);
	};
};

//pragma mark Public

void FileRequest::AddHandler(FileRequestHandler *handler, void *data,
	bool takeOwnership) {
	if (handler == NULL) return;
	fHandler.push_back(new RequestHandlerInfo(handler, data, takeOwnership));
};

status_t FileRequest::Start(void) {
	if (fStorageStatus != B_OK || fCurl == NULL || fCurlStatus != CURLE_OK) {
		if (fCurlStatus != CURLE_OK)
			fprintf(stderr, "FileRequest::Start - curl setup failed: %s\n",
				curl_easy_strerror(fCurlStatus));
		fResult = fCurlStatus == CURLE_OK ? CURLE_FAILED_INIT : fCurlStatus;
		fEnd = time(NULL);
		fComplete = true;
		status_t result = fStorageStatus != B_OK ? fStorageStatus : B_ERROR;
		ReleaseNetworkResources();
		return result;
	}

	fStart = time(NULL);
	status_t result = B_ERROR;
	
	CURLcode curlResult = curl_easy_perform(fCurl);
	fResult = curlResult;
	curl_easy_getinfo(fCurl, CURLINFO_RESPONSE_CODE, &fHTTPStatus);
	if (curlResult != CURLE_OK) {
		if (curlResult == CURLE_RANGE_ERROR) {
			fprintf(stderr, "FileRequest::Start - %s couldn't be resumed, restarting\n", fURL.String());
			fOffset = 0;

			status_t reset = B_OK;
			if (fTemporaryFD >= 0) {
				if (::ftruncate(fTemporaryFD, 0) != 0
					|| ::lseek(fTemporaryFD, 0, SEEK_SET) < 0)
					reset = B_FILE_ERROR;
			} else {
				reset = fFile.SetSize(0);
				if (reset == B_OK && fFile.Seek(0, SEEK_SET) < 0)
					reset = B_FILE_ERROR;
			}

			if (reset == B_OK) {
				Init();
				return Start();
			} else {
				result = reset;
			}
		};
		if (curlResult == CURLE_ABORTED_BY_CALLBACK) result = DownloadCancelled;
	} else {
		result = B_OK;
	};
	
	fEnd = time(NULL);
	fComplete = true;
	ReleaseNetworkResources();
	
	return result;
};

void FileRequest::Cancel(void) {
	fCancelDownload.store(true);
};

const char *FileRequest::URL(void) {
	return fURL.String();
};

int32 FileRequest::ExpectedSize(void) {
	return fExpectedSize > INT32_MAX ? INT32_MAX
		: static_cast<int32>(fExpectedSize);
};

float FileRequest::Speed(void) {
	time_t end = fEnd;
	int32 size = Size();
	
	if (fComplete == false) end = time(NULL);
	
	float duration = end - fStart;
	if (duration == 0) duration = 1;
	float speed = ((float)size) / duration;

	return speed;
};

time_t FileRequest::StartTime(void) {
	return fStart;
};

time_t FileRequest::EndTime(void) {
	return fEnd;
};

time_t FileRequest::Duration(void) {
	time_t end = fEnd;
	if (fComplete == false) end = time(NULL);

	return end - fStart;
};

int32 FileRequest::Size(void) {
	return fSize > INT32_MAX ? INT32_MAX : static_cast<int32>(fSize);
};

entry_ref FileRequest::Ref(void) {
	return fRef;
};

ssize_t FileRequest::Read(void *buffer, size_t size) {
	size_t total = 0;

	if (fTemporaryFD >= 0) {
		if (::lseek(fTemporaryFD, 0, SEEK_SET) < 0)
			return B_FILE_ERROR;

		while (total < size) {
			ssize_t bytes = ::read(fTemporaryFD,
				static_cast<char *>(buffer) + total, size - total);
			if (bytes <= 0)
				return total > 0 ? static_cast<ssize_t>(total) : bytes;
			total += bytes;
		}
		return static_cast<ssize_t>(total);
	}

	while (total < size) {
		ssize_t bytes = fFile.ReadAt(total,
			static_cast<char *>(buffer) + total, size - total);
		if (bytes <= 0)
			return total > 0 ? static_cast<ssize_t>(total) : bytes;
		total += bytes;
	}
	return static_cast<ssize_t>(total);
};

void FileRequest::ReleaseNetworkResources(void) {
	if (fCurl != NULL) {
		curl_easy_cleanup(fCurl);
		fCurl = NULL;
	}
};

status_t FileRequest::ReleaseTemporaryStorage(void) {
	if (fTemporaryFD >= 0) {
		status_t result = ::close(fTemporaryFD) == 0 ? B_OK : B_FILE_ERROR;
		fTemporaryFD = -1;
		return result;
	}
	return B_OK;
};

bool FileRequest::UsesTemporaryStorage(void) const {
	return fTemporaryFD >= 0;
};

status_t FileRequest::StorageStatus(void) const {
	return fStorageStatus;
};

bool FileRequest::Completed(void) {
	return fComplete;
};

const char *FileRequest::MIME(void) {
	return fMIME.String();
};

CURLcode FileRequest::CurlStatus(void) const {
	return fResult;
};

long FileRequest::HTTPStatus(void) const {
	return fHTTPStatus;
};

const char *FileRequest::Error(void) const {
	if (fStorageStatus != B_OK)
		return strerror(fStorageStatus);
	return fCurlError[0] == '\0' ? curl_easy_strerror(fResult) : fCurlError;
};

BMessage FileRequest::ProgressMessage(void) {
	return fProgress;
};

//#pragma mark Private

void FileRequest::Init(void) {
	if (fCurl != NULL) {
		curl_easy_cleanup(fCurl);
		fCurl = NULL;
	}

	fStart = 0;
	fEnd = -1;
	fExpectedSize = 0;
	fComplete = false;
	fSize = fOffset;
	fMIME = "";
	fStartSent = false;
	fCancelDownload.store(false);
	fCurlStatus = CURLE_OK;
	fResult = CURLE_OK;
	fHTTPStatus = 0;
	fCurlError[0] = '\0';
	
	fCurl = curl_easy_init();
	if (fCurl == NULL) {
		fCurlStatus = CURLE_FAILED_INIT;
		return;
	}

#define SET_CURL_OPTION(option, value) \
	do { \
		CURLcode optionStatus = curl_easy_setopt(fCurl, option, value); \
		if (fCurlStatus == CURLE_OK && optionStatus != CURLE_OK) \
			fCurlStatus = optionStatus; \
	} while (0)

	SET_CURL_OPTION(CURLOPT_NOSIGNAL, 1L);
	SET_CURL_OPTION(CURLOPT_ERRORBUFFER, fCurlError);
	SET_CURL_OPTION(CURLOPT_URL, fURL.String());
	SET_CURL_OPTION(CURLOPT_PROTOCOLS_STR, "file,http,https");
	SET_CURL_OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	SET_CURL_OPTION(CURLOPT_WRITEFUNCTION, SaveData);
	SET_CURL_OPTION(CURLOPT_WRITEDATA, this);
	SET_CURL_OPTION(CURLOPT_HEADERFUNCTION, ParseHeader);
	SET_CURL_OPTION(CURLOPT_HEADERDATA, this);
	SET_CURL_OPTION(CURLOPT_FOLLOWLOCATION, 1L);
	SET_CURL_OPTION(CURLOPT_MAXREDIRS, 10L);
	SET_CURL_OPTION(CURLOPT_FAILONERROR, 1L);
	SET_CURL_OPTION(CURLOPT_CONNECTTIMEOUT, 30L);
	SET_CURL_OPTION(CURLOPT_SSL_VERIFYPEER, 1L);
	SET_CURL_OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
	SET_CURL_OPTION(CURLOPT_ACCEPT_ENCODING, "");
	SET_CURL_OPTION(CURLOPT_USERAGENT, "FeedKit/0.1");
	SET_CURL_OPTION(CURLOPT_NOPROGRESS, 0L);
	SET_CURL_OPTION(CURLOPT_XFERINFOFUNCTION, Progress);
	SET_CURL_OPTION(CURLOPT_XFERINFODATA, this);
	
	if (fOffset > 0) {
		SET_CURL_OPTION(CURLOPT_RESUME_FROM_LARGE,
			static_cast<curl_off_t>(fOffset));
		fprintf(stderr, "FileRequest::Init() Will attempt to restart download at %i byte(s)\n", fOffset);
	}

#undef SET_CURL_OPTION
};

//#pragma mark LibCurl Hooks

size_t FileRequest::ParseHeader(void *header, size_t size, size_t length, void *ptr) {
	FileRequest *us = reinterpret_cast<FileRequest *>(ptr);
	if (size != 0 && length > static_cast<size_t>(-1) / size)
		return 0;
	size_t bytes = size * length;
		
	BString headerStr((char *)header, bytes);
	int32 colon = headerStr.IFindFirst(":");

	if (colon != B_ERROR) {
		BString value(headerStr);
		headerStr.Truncate(colon);
		value.Remove(0, colon + 2);

		if (headerStr.ICompare("Content-Length") == 0) {
			value.IReplaceAll("\r", "");
			value.IReplaceAll("\n", "");
			value.IReplaceAll(" ", "");
			curl_off_t contentLength = strtoll(value.String(), NULL, 10);
			if (contentLength >= 0
				&& contentLength <= std::numeric_limits<curl_off_t>::max()
					- us->fOffset)
				us->fExpectedSize = us->fOffset + contentLength;

		};
		if (headerStr.ICompare("Content-Type") == 0) {
			value.IReplaceAll("\r", "");
			value.IReplaceAll("\n", "");
			value.IReplaceAll(" ", "");
			
			us->fMIME = value;
		};
	};

	return bytes;
};

size_t FileRequest::SaveData(void *data, size_t size, size_t length, void *ptr) {
	FileRequest *us = reinterpret_cast<FileRequest *>(ptr);
	if (size != 0 && length > static_cast<size_t>(-1) / size)
		return 0;
	size_t bytes = size * length;
	ssize_t written = us->fTemporaryFD >= 0
		? ::write(us->fTemporaryFD, data, bytes) : us->fFile.Write(data, bytes);
	if (written < 0)
		return 0;

	us->fSize += static_cast<curl_off_t>(written);

	if (us->fStartSent == false) {
		for (handlerinfo_t::iterator rIt = us->fHandler.begin(); rIt != us->fHandler.end(); rIt++) {
			(*rIt)->DownloadStarted(us);
		};
		
		us->fStartSent = true;
	};

	return static_cast<size_t>(written);
};

int FileRequest::Progress(void *data, curl_off_t downTotal, curl_off_t downNow,
	curl_off_t upTotal, curl_off_t upNow) {
	FileRequest *us = reinterpret_cast<FileRequest *>(data);
	(void)upTotal;
	(void)upNow;
	if (downTotal > 0
		&& downTotal <= std::numeric_limits<curl_off_t>::max() - us->fOffset)
		us->fExpectedSize = us->fOffset + downTotal;

	for (handlerinfo_t::iterator rIt = us->fHandler.begin(); rIt != us->fHandler.end(); rIt++) {
		(*rIt)->DownloadProgress(us, static_cast<double>(downTotal),
			static_cast<double>(downNow));
	};
	

	return us->fCancelDownload.load() ? 1 : 0;
};

