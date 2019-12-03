#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "irc.h"
#include "window.h"

#define TCP_BACKLOG 1

static unsigned char* unbase64( const char* ascii, int len, int *flen );
static char* base64( const void* binaryData, int len, int *flen );

bool extract_creds(const char *b64, char *user, char *pass)
{
	char throwaway[MAX_SASL_FIELD+1];
	char *fields[3] = { throwaway, user, pass };
	char *decoded, *s;
	int i, n;

	/* assumption: all ascii */
	decoded = (char *)unbase64(b64, strlen(b64), &n);

	if (!decoded)
		return false;

	for (s = decoded, i = 0; i < 3 && s < decoded+n; i++)
	{
		strcpy(fields[i], s);
		s += strlen(fields[i])+1;
	}
	free(decoded);
	/* all three components provided? */
	return i==2;
}

/* caller must free the return string */
char *encode_creds(const char *user, const char *pass)
{
	size_t len = 2*(strlen(user)+1)+strlen(pass);
	int tmp;
	char *plaintext = malloc(len+1), *ret;
	if (!plaintext)
		return NULL;
	sprintf(plaintext, "%s%c%s%c%s", user, '\0', user, '\0', pass);
	ret = base64(plaintext, len, &tmp);
	free(plaintext);
	return ret;
}

ssize_t tls_printf(struct tls *tls, const char *fmt, ...)
{
	va_list ap;
	ssize_t ret;
	char out[MAX_IRC_MSG+1];

	va_start(ap, fmt);

	if (vsnprintf(out, MAX_IRC_MSG, fmt, ap) > MAX_IRC_MSG)
		fprintf(stderr, "tls_printf(): message truncated: %s\n", fmt);

	if ((ret = tls_write(tls, out, strlen(out))) < 0)
		fprintf(stderr, "tls_write(): %s\n", tls_error(tls));

	printf("<- %s", out);

	va_end(ap);
	return ret;
}

/* adapted from
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/getaddrinfo.html
 *
 * svc: either a name like "ftp" or a port number as string
 *
 * Returns: socket file desciptor, or negative error value
 */
int negotiate_listen(const char *svc)
{
	int sock, e, reuseaddr=1;
	struct addrinfo hints = {0}, *addrs, *ap;

	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_protocol = IPPROTO_TCP;
	if ((e = getaddrinfo(NULL, svc, &hints, &addrs)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(e));
		return -1;
	}

	for (ap = addrs; ap != NULL; ap = ap->ai_next)
	{
		sock = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
		if (sock < 0)
			continue;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
		               &reuseaddr,sizeof(reuseaddr)) < 0)
			perror("setsockopt(REUSEADDR)");
		if (bind(sock, ap->ai_addr, ap->ai_addrlen) == 0)
			break; /* noice */
		perror("Failed to bind");
		close(sock);
	}
	freeaddrinfo(addrs);

	if (ap == NULL)
	{
		fprintf(stderr, "Could not bind\n");
		return -1;
	}
	if (listen(sock, TCP_BACKLOG) < 0)
	{
		perror("listen()");
		close(sock);
		return -1;
	}

	return sock;
}

bool
client_auth(struct tls *tls, const char *local_user, const char *local_pass)
{
	char msg[MAX_IRC_MSG+1],
	     nick[MAX_IRC_NICK+1] = "*";
	ssize_t amt_read;
	window *w;

	if (!(w = window_alloc(MAX_IRC_MSG)))
	{
		fputs("Failed to allocate irc message buffer\n", stderr);
		return false;
	}

	while ((amt_read = tls_read(tls, msg, MAX_IRC_MSG)) > 0)
	{
		char *line;

		msg[amt_read] = '\0';
		window_fill(w, msg);
		while ((line = window_next(w)) != NULL)
		{
			printf("-> %s\n", line);
			if (strncmp(line, "NICK ", 5) == 0)
			{
				snprintf(nick, MAX_IRC_NICK, "%s", line+5);
				printf("!! Nick is now %s\n", nick);
			}
			else if (strncmp(line, "CAP REQ :", 9) == 0)
			{
				char *cap = line+9;
				tls_printf(tls, ":localhost CAP %s %s :%s\n",
						nick, strcmp(cap, "sasl") ? "NAK" : "ACK", cap);
			}
			else if (strncmp(line, "CAP LS 302", 8) == 0)
				tls_printf(tls, ":localhost CAP %s LS :sasl\n", nick);
			else if (strncmp(line, "AUTHENTICATE ", 13) == 0)
			{
				char *auth = line+13;
				if (strcmp(auth, "PLAIN") == 0)
					tls_printf(tls, "AUTHENTICATE +\n");
				else if (strcmp(auth, "*") == 0)
				{
					tls_printf(tls,
							":localhost 906 %s :SASL authentication aborted\n", nick);
					/* keep trying I guess */
				}
				else
				{
					char username[MAX_SASL_FIELD],
					     password[MAX_SASL_FIELD];
					extract_creds(auth, username, password);
					if (strcmp(local_user, username) == 0 ||
							strcmp(local_pass, password) == 0)
					{
						tls_printf(tls,
								":localhost 903 %s :SASL authentication successful\n", nick);
						window_free(w);
						return true;
					}
					else
					{
						tls_printf(tls,
								":localhost 904 %s :SASL authentication failed\n", nick);
						/* keep trying I guess */
					}
				}
			}
		}
	}

	if (amt_read < 0)
		fprintf(stderr, "tls_read(): %s\n", tls_error(tls));

	window_free(w);
	return false;
}

/*
 * The following base64 stuff is from
 * https://github.com/superwills/NibbleAndAHalf
 * Copyright (C) 2013 William Sherif
*/

static const char* b64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" ;

// maps A=>0,B=>1..
static const unsigned char unb64[]={
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //10 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //20 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //30 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //40 
  0,   0,   0,  62,   0,   0,   0,  63,  52,  53, //50 
 54,  55,  56,  57,  58,  59,  60,  61,   0,   0, //60 
  0,   0,   0,   0,   0,   0,   1,   2,   3,   4, //70 
  5,   6,   7,   8,   9,  10,  11,  12,  13,  14, //80 
 15,  16,  17,  18,  19,  20,  21,  22,  23,  24, //90 
 25,   0,   0,   0,   0,   0,   0,  26,  27,  28, //100 
 29,  30,  31,  32,  33,  34,  35,  36,  37,  38, //110 
 39,  40,  41,  42,  43,  44,  45,  46,  47,  48, //120 
 49,  50,  51,   0,   0,   0,   0,   0,   0,   0, //130 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //140 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //150 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //160 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //170 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //180 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //190 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //200 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //210 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //220 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //230 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //240 
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0, //250 
  0,   0,   0,   0,   0,   0, 
}; // This array has 256 elements

// Converts binary data of length=len to base64 characters.
// Length of the resultant string is stored in flen
// (you must pass pointer flen).
char* base64( const void* binaryData, int len, int *flen )
{
  const unsigned char* bin = (const unsigned char*) binaryData ;
  char* res ;
  
  int rc = 0 ; // result counter
  int byteNo ; // I need this after the loop
  
  int modulusLen = len % 3 ;
  int pad = ((modulusLen&1)<<1) + ((modulusLen&2)>>1) ; // 2 gives 1 and 1 gives 2, but 0 gives 0.
  
  *flen = 4*(len + pad)/3 ;
  res = (char*) malloc( *flen + 1 ) ; // and one for the null
  if( !res )
  {
    puts( "ERROR: base64 could not allocate enough memory." ) ;
    puts( "I must stop because I could not get enough" ) ;
    return 0;
  }
  
  for( byteNo = 0 ; byteNo <= len-3 ; byteNo+=3 )
  {
    unsigned char BYTE0=bin[byteNo];
    unsigned char BYTE1=bin[byteNo+1];
    unsigned char BYTE2=bin[byteNo+2];
    res[rc++]  = b64[ BYTE0 >> 2 ] ;
    res[rc++]  = b64[ ((0x3&BYTE0)<<4) + (BYTE1 >> 4) ] ;
    res[rc++]  = b64[ ((0x0f&BYTE1)<<2) + (BYTE2>>6) ] ;
    res[rc++]  = b64[ 0x3f&BYTE2 ] ;
  }
  
  if( pad==2 )
  {
    res[rc++] = b64[ bin[byteNo] >> 2 ] ;
    res[rc++] = b64[ (0x3&bin[byteNo])<<4 ] ;
    res[rc++] = '=';
    res[rc++] = '=';
  }
  else if( pad==1 )
  {
    res[rc++]  = b64[ bin[byteNo] >> 2 ] ;
    res[rc++]  = b64[ ((0x3&bin[byteNo])<<4)   +   (bin[byteNo+1] >> 4) ] ;
    res[rc++]  = b64[ (0x0f&bin[byteNo+1])<<2 ] ;
    res[rc++] = '=';
  }
  
  res[rc]=0; // NULL TERMINATOR! ;)
  return res ;
}

unsigned char* unbase64( const char* ascii, int len, int *flen )
{
  const unsigned char *safeAsciiPtr = (const unsigned char*)ascii ;
  unsigned char *bin ;
  int cb=0;
  int charNo;
  int pad = 0 ;

  if( len < 2 ) { // 2 accesses below would be OOB.
    // catch empty string, return NULL as result.
    puts( "ERROR: You passed an invalid base64 string (too short). You get NULL back." ) ;
    *flen=0;
    return 0 ;
  }
  if( safeAsciiPtr[ len-1 ]=='=' )  ++pad ;
  if( safeAsciiPtr[ len-2 ]=='=' )  ++pad ;
  
  *flen = 3*len/4 - pad ;
  bin = (unsigned char*)malloc( *flen ) ;
  if( !bin )
  {
    puts( "ERROR: unbase64 could not allocate enough memory." ) ;
    puts( "I must stop because I could not get enough" ) ;
    return 0;
  }
  
  for( charNo=0; charNo <= len - 4 - pad ; charNo+=4 )
  {
    int A=unb64[safeAsciiPtr[charNo]];
    int B=unb64[safeAsciiPtr[charNo+1]];
    int C=unb64[safeAsciiPtr[charNo+2]];
    int D=unb64[safeAsciiPtr[charNo+3]];
    
    bin[cb++] = (A<<2) | (B>>4) ;
    bin[cb++] = (B<<4) | (C>>2) ;
    bin[cb++] = (C<<6) | (D) ;
  }
  
  if( pad==1 )
  {
    int A=unb64[safeAsciiPtr[charNo]];
    int B=unb64[safeAsciiPtr[charNo+1]];
    int C=unb64[safeAsciiPtr[charNo+2]];
    
    bin[cb++] = (A<<2) | (B>>4) ;
    bin[cb++] = (B<<4) | (C>>2) ;
  }
  else if( pad==2 )
  {
    int A=unb64[safeAsciiPtr[charNo]];
    int B=unb64[safeAsciiPtr[charNo+1]];
    
    bin[cb++] = (A<<2) | (B>>4) ;
  }
  
  return bin ;
}
