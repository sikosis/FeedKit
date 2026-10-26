#ifndef LIBFEEDKIT_SHA1_H
#define LIBFEEDKIT_SHA1_H

#include <String.h>
#include <SupportDefs.h>

namespace FeedKit {
	namespace Private {
		status_t SHA1ToHex(const char *data, size_t length, BString *hex);
	}
}

#endif
