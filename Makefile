OBJS = window.o sasl.o config.o

CFLAGS = -std=c99 -g -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wshadow
LDFLAGS = -ltls -pthread

.SUFFIXES :
.SUFFIXES : .o .c

picobounce : picobounce.c $(OBJS)
	cc $(CFLAGS) -o picobounce picobounce.c $(OBJS) $(LDFLAGS)

window.o : window.c window.h

config.o : config.c config.h

sasl.o : sasl.c sasl.h

clean :
	rm picobounce *.o
