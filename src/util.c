#include "util.h"
#include <string.h>

int json_str(const char *haystack, const char *key, char *out, size_t outsz) {
	const char *p = strstr(haystack, key);
	if (!p)
		return 0;
	p += strlen(key);
	const char *q1 = strchr(p, '"');
	if (!q1)
		return 0;
	const char *q2 = strchr(q1 + 1, '"');
	if (!q2)
		return 0;
	size_t len = (size_t)(q2 - q1 - 1);
	if (len >= outsz)
		len = outsz - 1;
	memcpy(out, q1 + 1, len);
	out[len] = '\0';
	return 1;
}
