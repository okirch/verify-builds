
CFLAGS	= -Wall -g -O2 -Werror -D_LARGEFILE64_SOURCE
OBJS	= ftreecmp.o fstate.o report.o
LINK	= -lelf

all:	ftreecmp

ftreecmp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LINK)

%.o: %.c fstate.h
	$(CC) $(CFLAGS) -c -o $@ $<
