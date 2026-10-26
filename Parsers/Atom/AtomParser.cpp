#include "AtomParser.h"

#include <parsedate.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libfeedkit/Content.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/FeedKitConstants.h>
#include <libfeedkit/Item.h>
#include <libfeedkit/SettingsManager.h>

#include <ctype.h>
#include <stdlib.h>

//#pragma mark Namespace import

using namespace FeedKit;
using namespace FeedKit::Settings;

//#pragma mark C Hooks

extern "C" {
	FeedParser *instantiate_parser(BMessage /*settings*/) {
		return new AtomParser();
	};
};

//#pragma mark Constants

#define NODE_NAME_EQ(node, name) (xmlStrcmp(node, (const xmlChar *)name) == 0)

const char *kNodeNames[] = {
	"",
    "XML_ELEMENT_NODE",
    "XML_ATTRIBUTE_NODE",
    "XML_TEXT_NODE",
    "XML_CDATA_SECTION_NODE",
    "XML_ENTITY_REF_NODE",
    "XML_ENTITY_NODE",
    "XML_PI_NODE",
    "XML_COMMENT_NODE",
    "XML_DOCUMENT_NODE",
    "XML_DOCUMENT_TYPE_NODE",
    "XML_DOCUMENT_FRAG_NODE",
    "XML_NOTATION_NODE",
    "XML_HTML_DOCUMENT_NODE",
    "XML_DTD_NODE",
    "XML_ELEMENT_DECL",
    "XML_ATTRIBUTE_DECL",
    "XML_ENTITY_DECL",
    "XML_NAMESPACE_DECL",
    "XML_XINCLUDE_START",
    "XML_XINCLUDE_END",
    "XML_DOC_DOCUMENT_NODE"
};

//#pragma mark Constructor

AtomParser::AtomParser(void) {
};

AtomParser::~AtomParser(void) {
};

//#pragma mark FeedParser Hooks

float AtomParser::HandlesFeedURL(const char */*url*/) {
	return 1.0f;
};

float AtomParser::HandlesFeed(const char */*url*/, const char *feed, int32 length) {
	float ability = 0.0f;
	xmlDocPtr doc = xmlParseMemory(feed, length);
	if (doc != NULL) {
		xmlXPathContextPtr pathCtxt = xmlXPathNewContext(doc);
	
		if (pathCtxt != NULL) {
			xmlXPathObjectPtr root = xmlXPathEvalExpression((const xmlChar *)"/", pathCtxt);
			if (root != NULL) {
				xmlNodeSetPtr nodes = root->nodesetval;
				for (int32 i = 0; nodes != NULL && i < nodes->nodeNr; i++) {
					xmlNode *node = nodes->nodeTab[i]->children;
					
					while (node != NULL) {
						if (node->type == XML_ELEMENT_NODE) {
							if (xmlStrcmp(node->name, (const xmlChar *)"feed") == 0) {
								ability = 1.0f;
								break;
							};
						};
						
						node = node->next;
					};
				};
				xmlXPathFreeObject(root);
			};
			xmlXPathFreeContext(pathCtxt);
		};
		xmlFreeDoc(doc);
	};

	return ability;
};

Feed *AtomParser::ParseFeed(const char *url, const char *data, int32 length) {
	Feed *feed = NULL;
	xmlDocPtr doc = xmlParseMemory(data, length);
	if (doc == NULL) return feed;

	xmlXPathContextPtr pathCtxt = xmlXPathNewContext(doc);

	if (pathCtxt != NULL) {
		// Create an FS safe file name
		int32 urlLen = strlen(url);
		BString safe = "";
		for (int32 i = 0; i < urlLen; i++) if (isalnum(url[i])) safe << url[i];

		bool convertLinks = false;
		SettingsManager *feedPref = NULL;
		if (getenv("FEEDKIT_INTEGRATION_TEST") == NULL) {
			// Setup the template for the feed.
			feedPref = new SettingsManager(AppTypes::SettingFeed, safe.String());
			BMessage tmplate;
			BMessage display;
			display.AddInt32("type", B_BOOL_TYPE);
			display.AddString("name", "convertlinks");
			display.AddString("label", "Convert links to enclosures");
			display.AddString("help", "Useful for Blogspot feeds");
			display.AddInt32("display_type", FeedKit::Settings::CheckBox);
			display.AddBool("default_value", false);

			tmplate.AddMessage("setting", &display);
			feedPref->Template(&tmplate);

			BMessage settings = feedPref->Settings();
			if (settings.FindBool("convertlinks", &convertLinks) != B_OK)
				convertLinks = false;
		}

		xmlXPathObjectPtr root = xmlXPathEvalExpression((const xmlChar *)"/", pathCtxt);
		if (root != NULL) {
			xmlNodeSetPtr nodes = root->nodesetval;
						
			for (int32 i = 0; nodes != NULL && i < nodes->nodeNr; i++) {
				xmlNode *node = nodes->nodeTab[i]->children;
				
				while (node != NULL) {
					if (node->type == XML_ELEMENT_NODE) {
						if (xmlStrcmp(node->name, (const xmlChar *)"feed") == 0) {
							if (feed == NULL) feed = new Feed(url);
						
							Channel *channel = new Channel();
							feed->AddChannel(channel);

							if (ParseAtomFeed(channel, node, convertLinks) == B_OK
								&& feedPref != NULL)
								feedPref->DisplayName(channel->Title());
						};
					};
					
					node = node->next;
				};
			};
		};

		if (root != NULL)
			xmlXPathFreeObject(root);
		delete feedPref;
	};
	if (pathCtxt != NULL)
		xmlXPathFreeContext(pathCtxt);
	xmlFreeDoc(doc);
	
	return feed;
};

const char *AtomParser::Name(void) {
	return "AtomParser";
};

const char *AtomParser::Description(void) {
	return "A parser to handle Atom feeds";
};

BMessage AtomParser::SettingsTemplate(void) {
	return BMessage();
};

BView *AtomParser::SettingsView(BMessage /*settings*/) {
	return NULL;
};

//#pragma mark Private

const char *AtomParser::NodeContents(xmlNode *node) {
	static BString temp = "";
	xmlBuffer *buff = xmlBufferCreate();
	if (buff == NULL) {
		temp = "";
		return temp.String();
	}

	xmlNodeBufGetContent(buff, node);
	temp.SetTo((const char *)xmlBufferContent(buff), xmlBufferLength(buff));
	xmlBufferFree(buff);

	return temp.String();
};

const char *AtomParser::ExtractChildNode(xmlNode *parent, const char *name) {
	xmlNode *node = parent->children;
	
	while (node != NULL) {
		if ((node->type == XML_ELEMENT_NODE) && (NODE_NAME_EQ(node->name, name))) {
			return NodeContents(node);
		};
		
		node = node->next;
	};
	
	return "";
};

status_t AtomParser::ParseAtomFeed(Channel *channel, xmlNode *parent, bool convertLinks) {
	status_t status = B_OK;

	xmlNode *node = parent->children;
	
	Enclosure *icon = NULL;
	Enclosure *logo = NULL;

	while (node != NULL) {
		if (node->type == XML_ELEMENT_NODE) {
			if (NODE_NAME_EQ(node->name, "author")) {
//				xmlNode *n = node->children;
				const char *author = ExtractChildNode(node, "name");
				channel->Editor(author);
				channel->Webmaster(author);
			};
			if (NODE_NAME_EQ(node->name, "category")) {
				xmlChar *term = xmlGetProp(node, (const xmlChar *)"term");
				if (term != NULL)
					channel->Category((const char *)term);
				xmlFree(term);
			};
			if (NODE_NAME_EQ(node->name, "generator")) {
				channel->Generator(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "icon")) {
				delete icon;
				icon = new Enclosure(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "id")) {
				channel->Link(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "logo")) {
				delete logo;
				logo = new Enclosure(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "rights")) {
				channel->Copyright(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "title")) {
				channel->Title(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "subtitle")) {
				channel->Description(NodeContents(node));
			};
			if (NODE_NAME_EQ(node->name, "updated")) {
				channel->PublishedDate(parsedate(NodeContents(node), -1));
			};

			if (NODE_NAME_EQ(node->name, "entry")) {
				Item *item = new Item();
				if (ParseAtomEntry(channel, item, node->children, convertLinks) == B_OK) {
					channel->AddItem(item);
				} else
					delete item;
			};
		};
		
		node = node->next;
	};
	
	if (icon) {
		channel->Image(icon);
		delete logo;
	} else if (logo) {
		channel->Image(logo);
	};
	
	return status;
};

status_t AtomParser::ParseAtomEntry(Channel *channel, Item *item, xmlNode *node,
	bool convertLinks) {
	
	status_t status = B_OK;
	bool hasAuthor = false;
	BString category = "";
	
	while (node != NULL) {
		if (node->type == XML_ELEMENT_NODE) {
			if (NODE_NAME_EQ(node->name, "author")) {
				item->Author(ExtractChildNode(node, "name"));
				hasAuthor = true;
			};

			if (NODE_NAME_EQ(node->name, "category")) category << NodeContents(node) << "/";

			if (NODE_NAME_EQ(node->name, "content")) {
				xmlChar *type = xmlGetProp(node, (const xmlChar *)"type");
				Content *content = new Content();
				content->MIMEType(type == NULL ? "" : (const char *)type);
				content->Text(NodeContents(node));
				
				item->AddContent(content);
				xmlFree(type);
			};

			if (NODE_NAME_EQ(node->name, "title")) item->Title(NodeContents(node));

			if (NODE_NAME_EQ(node->name, "summary")) {
				xmlChar *type = xmlGetProp(node, (const xmlChar *)"type");
				Content *content = new Content();
				content->MIMEType(type == NULL ? "" : (const char *)type);
				content->Text(NodeContents(node));
				item->AddContent(content);
				xmlFree(type);
			};
			
			if (NODE_NAME_EQ(node->name, "id")) {
				item->GUID(NodeContents(node));
				item->IsGUIDPermaLink(true);
			};

			if (NODE_NAME_EQ(node->name, "link")) {
				xmlChar *rel = xmlGetProp(node, (const xmlChar *)"rel");
				xmlChar *href = xmlGetProp(node, (const xmlChar *)"href");
				xmlChar *title = xmlGetProp(node, (const xmlChar *)"title");

				if (rel != NULL && href != NULL
					&& xmlStrcmp(rel, (const xmlChar *)"alternate") == 0)
					item->Link((const char *)href);
				if (rel != NULL && href != NULL
					&& xmlStrcmp(rel, (const xmlChar *)"self") == 0) {
					item->SourceURL((const char *)href);
					if (title != NULL)
						item->SourceTitle((const char *)title);
				};
				if (rel != NULL && href != NULL
					&& xmlStrcmp(rel, (const xmlChar *)"enclosure") == 0) {
					xmlChar *mime = xmlGetProp(node, (const xmlChar *)"type");
					xmlChar *lenStr = xmlGetProp(node, (const xmlChar *)"length");
					int32 size = -1;
					if (lenStr != NULL && xmlStrlen(lenStr) > 0)
						size = atol((const char *)lenStr);
					
					Enclosure *enc = new Enclosure((const char *)href,
						(const char *)mime, (const char *)title, size);
					item->AddEnclosure(enc);
					xmlFree(mime);
					xmlFree(lenStr);
				};
				// XXX - Not really correct, but Blogger.com does it
				if (convertLinks && rel != NULL && href != NULL
					&& xmlStrcmp(rel, (const xmlChar *)"related") == 0) {
					xmlChar *mime = xmlGetProp(node, (const xmlChar *)"type");
					xmlChar *lenStr = xmlGetProp(node, (const xmlChar *)"length");
					int32 size = -1;
					if (lenStr != NULL && xmlStrlen(lenStr) > 0)
						size = atol((const char *)lenStr);
					
					Enclosure *enc = new Enclosure((const char *)href,
						(const char *)mime, (const char *)title, size);
					item->AddEnclosure(enc);
					xmlFree(mime);
					xmlFree(lenStr);
				};
				xmlFree(rel);
				xmlFree(href);
				xmlFree(title);
			};

			if (NODE_NAME_EQ(node->name, "updated")) item->Date(parsedate(NodeContents(node), -1));
		};
		
		node = node->next;
	};
	
	// If the entry has no author, inherit from the channel
	if (hasAuthor == false) item->Author(channel->Editor());

	// Trim the trailing / from the category list
	if (category.Length() > 0) category.Truncate(category.Length() - 1);
	item->Category(category.String());
	
	return status;
};


