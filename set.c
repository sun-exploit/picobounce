#include <stdlib.h>
#include <string.h>

#include "set.h"

#define HASHSZ 101

set set_alloc(void)
{
	set s = malloc(HASHSZ * (sizeof *s));
	size_t i;
	if (!s)
		return NULL;
	for (i = 0; i < HASHSZ; i++)
		s[i] = NULL;
	return s;
}

static unsigned long
djb2hash(const unsigned char *str)
{
	unsigned long hash = 5381;
	int c;

	if (str)
		while ( (c = *str++) )
			hash = hash * 33 + c;
	return hash;
}

static struct bucket *set_lookup(set s, char *key)
{
	struct bucket* np;
	for (np = s[djb2hash(key) % HASHSZ]; np; np = np->next)
		if (strcmp(key, np->key) == 0)
			return np;
	return NULL;
}

bool set_contains(set s, char *key)
{
	return set_lookup(s, key) != NULL;
}

bool set_add(set s, char *key)
{
	struct bucket *np;
	unsigned long h;

	if ((np = set_lookup(s, key)) == NULL)
	{
		np = malloc(sizeof(*np));
		if (np == NULL || (np->key = strdup(key)) == NULL)
			return false;
		h = djb2hash(key) % HASHSZ;
		np->next = s[h];
		s[h] = np;
	}
	return true;
}

void set_rm(set s, char *key)
{
	struct bucket *np, *prev;
	unsigned long h = djb2hash(key) % HASHSZ;
	if ((np = s[h]) == NULL)
		return;
	for (prev = NULL; np; np = np->next, prev = np)
	{
		if (strcmp(key, np->key) == 0)
		{
			if (prev == NULL)
				s[h] = np->next;
			else
				prev->next = np->next;
			free(np->key);
			free(np);
			break;
		}
	}
}