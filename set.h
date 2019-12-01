#ifndef PICOBOUNCE_SET_H
#define PICOBOUNCE_SET_H

#include <stdbool.h>

struct bucket
{
	struct bucket *next;
	char *key;
};

typedef struct bucket **set;

set set_alloc(void);

bool set_contains(set s, char *key);

bool set_add(set s, char *key);

void set_rm(set s, char *key);

#endif