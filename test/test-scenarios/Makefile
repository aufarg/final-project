CFLAGS      = -Wall
LDFLAGS     = -lm -lrt -ljansson

PROG    = server
SRC     = server.c
OBJ     = ${SRC:.c=.o}

all: build

%.o: %.c
	${CC} -c ${CFLAGS} $<

build: ${OBJ}
	${CC} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm ${OBJ}

