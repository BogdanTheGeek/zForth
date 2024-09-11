
BIN := z4c
SRC := main.c zforth.c

OBJS := $(subst .c,.o, $(SRC))
DEPS := $(subst .c,.d, $(SRC))

CC	:= cc

VPATH := ../zforth
CFLAGS += -I. -I../zforth
CFLAGS += -Os -g -pedantic -MMD
CFLAGS += -fsanitize=address -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-result
LDFLAGS += -fsanitize=address -g 

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(BIN) $(OBJS) $(DEPS)

-include $(DEPS)

