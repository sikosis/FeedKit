#include "RSSParser.h"

#include <parsedate.h>

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <libfeedkit/FeedKitConstants.h>
#include <libfeedkit/Enclosure.h>
#include <libfeedkit/Item.h>

//#pragma mark Namespace import

using namespace FeedKit;
using namespace FeedKit::Settings;

//#pragma mark C Hooks

extern "C" {
	FeedParser *instantiate_parser(BMessage settings) {
		(void)settings;
		return new RSSParser();
	};
};

//#pragma mark Constants

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

const xmlChar *kMediaURI = (const xmlChar *)"http://search.yahoo.com/mrss";

//#pragma mark Constructor

RSSParser::RSSParser(void) {
};

RSSParser::~RSSParser(void) {
};

//#pragma mark FeedParser Hooks

float RSSParser::HandlesFeedURL(const char *url) {
	(void)url;
	return 1.0f;
};

float RSSParser::HandlesFeed(const char *url, const char *feed, int32 length) {
	(void)url;
	float ability = 0.0f;
	xmlDocPtr doc = xmlParseMemory(feed, length);
	if (doc != NULL) {
		xmlXPathContextPtr pathCtxt = xmlXPathNewContext(doc);
		if (pathCtxt != NULL) {
			xmlXPathObjectPtr root = xmlXPathEvalExpression((const xmlChar *)"/rss/channel", pathCtxt);
			
			if (root != NULL && root->nodesetval != NULL
				&& root->nodesetval->nodeNr > 0)
				ability = 1.0f;
			
			if (root != NULL)
				xmlXPathFreeObject(root);
		};
		if (pathCtxt != NULL)
			xmlXPathFreeContext(pathCtxt);
		xmlFreeDoc(doc);
	};

	return ability;
};

Feed *RSSParser::ParseFeed(const char *url, const char *data, int32 length) {
	Feed *feed = NULL;
	Channel *channel = NULL;
	xmlDocPtr doc = xmlParseMemory(data, length);
	if (doc == NULL) return feed;

	xmlXPathContextPtr pathCtxt = xmlXPathNewContext(doc);
	if (pathCtxt != NULL) {
		xmlXPathObjectPtr root = xmlXPathEvalExpression((const xmlChar *)"/rss/channel", pathCtxt);
	
		if (root != NULL && root->nodesetval != NULL
			&& root->nodesetval->nodeNr > 0) {
			xmlNodeSetPtr channels = root->nodesetval;
			
			feed = new Feed(url);
			channel = new Channel();
			feed->AddChannel(channel);

			for (int32 i = 0; i < channels->nodeNr; i++) {
				xmlNode *n = channels->nodeTab[i]->children;

				while (n != NULL) {
					if (n->type == XML_ELEMENT_NODE) {
						ExtractChannelNodeDetails(channel, n);
					};
					n = n->next;
				};
			};
		};
		if (root != NULL)
			xmlXPathFreeObject(root);
	};

	if (pathCtxt != NULL)
		xmlXPathFreeContext(pathCtxt);
	xmlFreeDoc(doc);
	
	return feed;
};

const char *RSSParser::Name(void) {
	return "RSSParser";
};

const char *RSSParser::Description(void) {
	return "A parser to handle RSS2.0 feeds";
};

//#pragma mark Private

BString RSSParser::NodeContents(xmlNode *node) {
	BString temp = "";
	xmlBuffer *buff = xmlBufferCreate();
	if (buff == NULL)
		return temp;

	xmlNodeBufGetContent(buff, node);
	temp.SetTo((const char *)xmlBufferContent(buff), xmlBufferLength(buff));
	xmlBufferFree(buff);

	return temp;
};

void RSSParser::ExtractChannelNodeDetails(Channel *channel, xmlNode *node) {
	if (xmlStrcmp(node->name, (const xmlChar *)"title") == 0) {
		channel->Title(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"link") == 0) {
		channel->Link(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"description") == 0) {
		channel->Description(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"language") == 0) {
		channel->Language(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"copyright") == 0) {
		channel->Copyright(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"managingEditor") == 0) {
		channel->Editor(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"webMaster") == 0) {
		channel->Webmaster(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"pubDate") == 0) {
		channel->PublishedDate(parsedate(NodeContents(node).String(), -1));
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"lastBuildDate") == 0) {
		channel->BuildDate(parsedate(NodeContents(node).String(), -1));
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"category") == 0) {
		channel->Category(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"generator") == 0) {
		channel->Generator(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"docs") == 0) {
		channel->Docs(NodeContents(node).String());
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"ttl") == 0) {
		channel->TTL(atol(NodeContents(node).String()));
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"image") == 0) {
		xmlNode *n = node->children;
		BString url;
		BString desc;
		while (n != NULL) {
			if (n->type == XML_ELEMENT_NODE) {
				if (xmlStrcmp(n->name, (const xmlChar *)"url") == 0) url = NodeContents(n);
				if (xmlStrcmp(n->name, (const xmlChar *)"title") == 0) desc = NodeContents(n);
			};
			n = n->next;
		};
			
		if (url.Length() > 0) {
			channel->Image(new Enclosure((char *)url.String(), NULL, (char *)desc.String()));
		};
		
		return;
	};
	if (xmlStrcmp(node->name, (const xmlChar *)"rating") == 0) {
		channel->PICSRating(NodeContents(node).String());
		return;
	};
	
	if (xmlStrcmp(node->name, (const xmlChar *)"item") == 0) {
		Item *item = new Item();	
		ExtractItemNodeDetails(item, node->children);
		
		channel->AddItem(item);
		
		return;
	};
};

void RSSParser::ExtractItemNodeDetails(Item *item, xmlNode *node) {
	while (node != NULL) {
//		if (node->ns == NULL) {
//			printf("%s: %s (NS: NONE)\n", node->name, kNodeNames[node->type]);
//		} else {
//			printf("%s: %s (NS: %s / %s)\n", node->name, kNodeNames[node->type],
//				node->ns->prefix, node->ns->href);
//		};
	
		if (node->type == XML_ELEMENT_NODE) {
			if (xmlStrcmp(node->name, (const xmlChar *)"title") == 0) {
				item->Title(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"description") == 0) {
				Content *content = new Content();
				content->Text(NodeContents(node).String());
				
				item->AddContent(content);
//				item->Description(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"link") == 0) {
				item->Link(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"guid") == 0) {
				item->GUID(NodeContents(node).String());
				xmlChar *permaStr = xmlGetProp(node, (const xmlChar *)"isPermaLink");
				bool perma = permaStr != NULL
					&& xmlStrcasecmp(permaStr, (const xmlChar *)"true") == 0;
				item->IsGUIDPermaLink(perma);
				xmlFree(permaStr);
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"author") == 0) {
				item->Author(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"category") == 0) {
				item->Category(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"pubDate") == 0) {
				item->Date(parsedate(NodeContents(node).String(), -1));
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"comments") == 0) {
				item->Comments(NodeContents(node).String());
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"enclosure") == 0) {
				xmlChar *url = xmlGetProp(node, (const xmlChar *)"url");
				xmlChar *mime = xmlGetProp(node, (const xmlChar *)"type");
				xmlChar *length = xmlGetProp(node, (const xmlChar *)"length");
				int32 enclosureSize = length == NULL ? -1
					: atol((const char *)length);
				if (url != NULL)
					item->AddEnclosure(new Enclosure((const char *)url,
						(const char *)mime, NULL, enclosureSize));
				xmlFree(url);
				xmlFree(mime);
				xmlFree(length);
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"source") == 0) {
				xmlChar *sourceURL = xmlGetProp(node, (const xmlChar *)"url");
				if (sourceURL != NULL)
					item->SourceURL((const char *)sourceURL);
				item->SourceTitle(NodeContents(node).String());
				xmlFree(sourceURL);
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"content") == 0) {
				xmlNs *ns = node->ns;
				if ((ns != NULL) && (xmlStrcmp(ns->href, kMediaURI) == 0)) {
					Enclosure *enclosure = ExtractMediaContent(node);
					if (enclosure) item->AddEnclosure(enclosure);
				};
			};
			if (xmlStrcmp(node->name, (const xmlChar *)"group") == 0) {
				xmlNs *ns = node->ns;
				if ((ns != NULL) && (xmlStrcmp(ns->href, kMediaURI) == 0)) {
					xmlNode *group = node->children;
	
					while (group != NULL) {
						Enclosure *enclosure = ExtractMediaContent(group);
						if (enclosure) item->AddEnclosure(enclosure);
	
						group = group->next;
					};
				};
			};
		};
		
		node = node->next;
	};
};

Enclosure *RSSParser::ExtractMediaContent(xmlNode *node) {
	if (node == NULL || node->type != XML_ELEMENT_NODE)
		return NULL;
	xmlChar *url = xmlGetProp(node, (const xmlChar *)"url");
	xmlChar *mime = xmlGetProp(node, (const xmlChar *)"type");
	const char *desc = NULL;
	xmlChar *sizeStr = xmlGetProp(node, (const xmlChar *)"fileSize");
	int32 size = -1;
	if (sizeStr != NULL && xmlStrlen(sizeStr) > 0)
		size = atol((const char *)sizeStr);

	Enclosure *enclosure = url == NULL ? NULL : new Enclosure((const char *)url,
		mime != NULL && xmlStrlen(mime) > 0 ? (const char *)mime : NULL,
		desc, size);
	xmlFree(url);
	xmlFree(mime);
	xmlFree(sizeStr);
	return enclosure;
};
