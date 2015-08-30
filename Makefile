bins := p2pd p2p-client
RM := rm -f

all : $(bins)

LIBEVENT_CFLAGS := $(shell pkg-config --cflags libevent 2>/dev/null)
LIBEV_CFLAGS := $(shell pkg-config --cflags libev 2>/dev/null)
EVENT_LIBS := $(shell pkg-config --libs libevent 2>/dev/null)
EV_LIBS := $(shell pkg-config --libs libev libev 2>/dev/null)

ifeq ($(EV_LIBS),)
$(error cannot find libev)
endif

ifeq ($(EVENT_LIBS),)
$(error cannot find libevent)
endif

CFLAGS := -g -Wall -Werror

p2pd : p2pd.c
	$(CC) $(CFLAGS) $(LIBEVENT_CFLAGS) -o $@ $< $(EVENT_LIBS)

p2p-client : p2p-client.c
	$(CC) $(CFLAGS) $(LIBEV_CFLAGS) -o $@ $< $(EV_LIBS)

clean:
	$(RM) $(bins)
