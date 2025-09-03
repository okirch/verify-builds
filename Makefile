
CFLAGS	= -Wall -g -O2 -Werror
OBJS	= ftreecmp.o fstate.o report.o

all:	ftreecmp

ftreecmp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c fstate.h
	$(CC) $(CFLAGS) -c -o $@ $<
