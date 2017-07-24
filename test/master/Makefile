CFLAGS      = -Wall
LDFLAGS     = -lm -lrt -ljansson -lxenctrl -lxenlight

PROG    = master
SRC     = master.c
OBJ     = ${SRC:.c=.o}

all: build

%.o: %.c
	${CC} -c ${CFLAGS} $<

build: ${OBJ}
	${CC} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm ${OBJ}

