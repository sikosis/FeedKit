#include "Enclosure.h"
#include "DownloadProgress.h"
#include "SHA1.h"

#include <Message.h>

//#pragma mark Namespace import

using namespace FeedKit;

//#pragma mark Constants

const type_code kTypeCode = 'tfke';

//#pragma mark Constructor

Enclosure::Enclosure(void)
	: fURL(""),
	fMIME(""),
	fDescription(""),
	fSize(-1),
	fState(None),
	fProgress(NULL),
	fLocalUUID("") {
};

Enclosure::Enclosure(const Enclosure &rhs)
	: BFlattenable(), 
	fURL(rhs.fURL),
	fMIME(rhs.fMIME), 
	fDescription(rhs.fDescription),
	fSize(rhs.fSize),
	fState(rhs.fState),
	fProgress(NULL),
	fLocalUUID("") {

	if (rhs.fProgress != NULL) {
		fProgress = new DownloadProgress(rhs.fProgress);
	};
	fLocalRef = rhs.fLocalRef;
};

Enclosure::Enclosure(const Enclosure * const rhs)
	: fURL(rhs->fURL),
	fMIME(rhs->fMIME),
	fDescription(rhs->fDescription),
	fSize(rhs->fSize),
	fState(rhs->fState),
	fProgress(NULL),
	fLocalUUID("") {

	if (rhs->fProgress != NULL) {
		fProgress = new DownloadProgress(rhs->fProgress);
	}	
	fLocalRef = rhs->fLocalRef;
};

Enclosure::Enclosure(const char *url, const char *mime, const char *description, int32 size)
	: fURL(url),
	fMIME(""),
	fDescription(""),
	fSize(size),
	fState(None),
	fProgress(NULL),
	fLocalUUID("") {

	if (mime != NULL) fMIME = mime;
	if (description != NULL) fDescription = description;
};

Enclosure::~Enclosure(void) {
	if (fProgress) delete fProgress;
};

//#pragma mark BFlattenable Hooks

status_t Enclosure::Flatten(void *buffer, ssize_t numBytes) const {
	BMessage flat;
	flat.AddString("url", fURL);
	flat.AddString("mime", fMIME);
	flat.AddString("description", fDescription);
	flat.AddInt32("size", fSize);
	flat.AddInt32("state", fState);
	if (fProgress) flat.AddFlat("progress", fProgress);
	flat.AddRef("localRef", &fLocalRef);
	
	return flat.Flatten((char *)buffer, numBytes);
};

status_t Enclosure::Unflatten(type_code /*code*/, const void *buffer, ssize_t /*numBytes*/) {
	BMessage flat;
	status_t ret = flat.Unflatten((char *)buffer);
	delete fProgress;
	fProgress = NULL;
	
	if (ret == B_OK) {
		// Compulsory
		if (flat.FindString("url", &fURL) != B_OK) ret = B_ERROR;

		// Optional
		if (flat.FindString("mime", &fMIME) != B_OK) fMIME = "";
		if (flat.FindString("description", &fDescription) != B_OK) fDescription = "";
		if (flat.FindInt32("size", &fSize) != B_OK) fSize = -1;
		
		int32 state;
		if (flat.FindInt32("state", &state) == B_OK)
			fState = static_cast<EnclosureState>(state);
		else
			fState = Error;
		flat.FindRef("localRef", &fLocalRef);

		DownloadProgress progress;
		if (flat.FindFlat("progress", &progress) == B_OK) {
			fProgress = new DownloadProgress(progress);
		};
	};
	
	return ret;
};

ssize_t Enclosure::FlattenedSize(void) const {
	BMessage flat;
	flat.AddString("url", fURL);
	flat.AddString("mime", fMIME);
	flat.AddString("description", fDescription);
	flat.AddInt32("size", fSize);
	flat.AddInt32("state", fState);
	if (fProgress) flat.AddFlat("progress", fProgress);
	flat.AddRef("localRef", &fLocalRef);

	return flat.FlattenedSize();
};

bool Enclosure::IsFixedSize(void) const {
	return false;
};

type_code Enclosure::TypeCode(void) const {
	return kTypeCode;
};

bool Enclosure::AllowsTypeCode(type_code code) const {
	return code == kTypeCode;
};

//#pragma mark Public

const char *Enclosure::URL(void) const {
	return fURL.String();
};

const char *Enclosure::MIME(void) const {
	return fMIME.String();
};

const char *Enclosure::Description(void) const {
	return fDescription.String();
};

int32 Enclosure::Size(void) const {
	return fSize;
};

//#pragma mark Download Information

EnclosureState Enclosure::State(void) const {
	return fState;
};

const DownloadProgress *Enclosure::Progress(void) const {
	return fProgress;
};

entry_ref Enclosure::LocalRef(void) const {
	return fLocalRef;
};

const char *Enclosure::UUID(void) const {
	return LocalUUID();
};

const char *Enclosure::LocalUUID(void) const {
	if (fLocalUUID.Length() == 0) {
		BString localID = "";
		
		localID << fURL << "#" << fMIME << "#" << fDescription << "#" << fSize;
		Private::SHA1ToHex(localID.String(), localID.Length(), &fLocalUUID);
	};
	
	return fLocalUUID.String();
};

//#pragma mark Operands

Enclosure &Enclosure::operator = (const Enclosure &rhs) {
	if (this == &rhs) return *this;

	delete fProgress;
	fURL = rhs.fURL;
	fMIME = rhs.fMIME;
	fDescription = rhs.fDescription;
	fSize = rhs.fSize;
	fState = rhs.fState;
	fProgress = rhs.fProgress == NULL
		? NULL : new DownloadProgress(rhs.fProgress);
	fLocalUUID = "";
	fLocalRef = rhs.fLocalRef;

	return *this;
};

bool Enclosure::operator == (const Enclosure &compare) const {
	return (strcmp(UUID(), compare.UUID()) == 0);
};

bool Enclosure::operator != (const Enclosure &compare) const {
	return (strcmp(UUID(), compare.UUID()) != 0);
};

//#pragma mark Protected

void Enclosure::SetLocalRef(entry_ref ref) {
	fLocalRef = ref;
};

void Enclosure::SetState(EnclosureState state) {
	fState = state;
};

void Enclosure::SetProgress(DownloadProgress *progress) {
	delete fProgress;
	fProgress = progress == NULL ? NULL : new DownloadProgress(progress);
};
