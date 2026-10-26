#ifndef PARSERMANAGER_H
#define PARSERMANAGER_H

#include <image.h>
#include <String.h>

#include <vector>
#include <map>
#include <set>

#include <libfeedkit/FeedKitConstants.h>

namespace FeedKit {
	class Feed;
	class FeedParser;
};

typedef std::vector<BString> path_t;
typedef std::map<image_id, FeedKit::FeedParser *> parser_t;

class ParserManager {
	public:
							ParserManager(void);
							ParserManager(const char *path);
							ParserManager(path_t paths);
							~ParserManager(void);
							
		FeedKit::Feed		*Parse(const char *url, const char *data, int32 length);
		int32				CountParsers(void) const;
	
	private:
		void				LoadAddons(const char *path);
		void				LoadStandardAddons(void);
		bool				HasParserNamed(const char *name) const;
	
		parser_t			fParsers;		
		std::set<BString>	fVisitedAddonPaths;
};

#endif

