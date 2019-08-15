#ifndef _SASL_H
#define _SASL_H

#include <stdbool.h>

/* max defined in RFC 4616 */
#define MAX_SASL_FIELD 255

bool extract_user_pass(char *b64, char *user, char *pass);

#endif