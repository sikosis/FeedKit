#ifndef FILEREQUESTHANDLER_H
#define FILEREQUESTHANDLER_H

class FileRequest;

class FileRequestHandler {
	public:
		virtual				~FileRequestHandler() {}
		virtual void		DownloadStarted(FileRequest */*request*/, void */*data*/) {}
		virtual void		DownloadProgress(FileRequest */*request*/, void */*data*/,
								double /*downTotal*/, double /*downCurrent*/) {};

};

#endif
