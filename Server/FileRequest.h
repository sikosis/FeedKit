#ifndef FILEREQUEST_H
#define FILEREQUEST_H

#include <Entry.h>
#include <File.h>
#include <String.h>
#include <Message.h>

#include <curl/curl.h>

#include <atomic>
#include <vector>

class FileRequestHandler;

enum FileRequestStatus {
	DownloadCancelled = CURLE_ABORTED_BY_CALLBACK,
};

class RequestHandlerInfo;

typedef std::vector<RequestHandlerInfo *> handlerinfo_t;

class FileRequest {
	public:
							FileRequest(const char *url, entry_ref ref, BMessage progress, int32 offset = 0);
							FileRequest(const char *url, BMessage progress = BMessage(), int32 offset = 0);
							~FileRequest(void);
		
		void				AddHandler(FileRequestHandler *handler, void *data,
								bool takeOwnership = true);
				
		status_t			Start(void);
		void				Cancel(void);
		
		const char			*URL(void);
		int32				ExpectedSize(void);
		float				Speed(void);
		time_t				StartTime(void);
		time_t				EndTime(void);
		time_t				Duration(void);
		int32				Size(void);
		entry_ref			Ref(void);
		ssize_t				Read(void *buffer, size_t size);
		void				ReleaseNetworkResources(void);
		status_t			ReleaseTemporaryStorage(void);
		bool				UsesTemporaryStorage(void) const;
		status_t			StorageStatus(void) const;
		bool				Completed(void);
		const char			*MIME(void);
		CURLcode			CurlStatus(void) const;
		long				HTTPStatus(void) const;
		const char			*Error(void) const;
		
		BMessage			ProgressMessage(void);

	private:
		void				Init(void);
	
		// LibCurl hooks				
		static size_t		ParseHeader(void *header, size_t size, size_t length, void *ptr);
		static size_t		SaveData(void *data, size_t size, size_t length, void *ptr);
			static int			Progress(void *data, curl_off_t downTotal,
									curl_off_t downNow, curl_off_t upTotal,
									curl_off_t upNow);

		BString				fURL;
		entry_ref			fRef;
		BFile				fFile;
		int					fTemporaryFD;
		status_t			fStorageStatus;
		CURL				*fCurl;
		CURLcode			fCurlStatus;
		CURLcode			fResult;
		long				fHTTPStatus;
		char				fCurlError[CURL_ERROR_SIZE];
		time_t				fStart;
		time_t				fEnd;
		curl_off_t			fExpectedSize;
		curl_off_t			fSize;
		bool				fComplete;
		BString				fMIME;
		BMessage			fProgress;
		bool				fStartSent;
		int32				fOffset;
		std::atomic<bool>	fCancelDownload;
		
		handlerinfo_t		fHandler;
};

#endif
