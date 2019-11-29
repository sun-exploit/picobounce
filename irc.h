#ifndef PICOBOUNCE_IRC_H
#define PICOBOUNCE_IRC_H

#include <stdbool.h>
#include <tls.h>

/* max defined in RFC 4616 */
#define MAX_SASL_FIELD 255

#define MAX_IRC_MSG  512
#define MAX_IRC_NICK 9

bool extract_creds(const char *b64, char *user, char *pass);
char *encode_creds(const char *user, const char *pass);
ssize_t tls_printf(struct tls *tls, const char *fmt, ...);

#endif