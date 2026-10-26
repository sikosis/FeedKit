#include "SHA1.h"

#include <openssl/evp.h>

using namespace FeedKit;

status_t
Private::SHA1ToHex(const char *data, size_t length, BString *hex)
{
	if (data == NULL || hex == NULL)
		return B_BAD_VALUE;

	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (context == NULL)
		return B_NO_MEMORY;

	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digestLength = 0;
	bool success = EVP_DigestInit_ex(context, EVP_sha1(), NULL) == 1
		&& EVP_DigestUpdate(context, data, length) == 1
		&& EVP_DigestFinal_ex(context, digest, &digestLength) == 1;
	EVP_MD_CTX_free(context);

	if (!success)
		return B_ERROR;

	static const char kHexDigits[] = "0123456789abcdef";
	char result[(EVP_MAX_MD_SIZE * 2) + 1];
	for (unsigned int i = 0; i < digestLength; i++) {
		result[i * 2] = kHexDigits[digest[i] >> 4];
		result[(i * 2) + 1] = kHexDigits[digest[i] & 0x0f];
	}
	result[digestLength * 2] = '\0';
	hex->SetTo(result);

	return B_OK;
}
