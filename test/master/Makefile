CFLAGS      = -Wall -O2
LDFLAGS     = -lm -lrt -ljansson -lxenctrl -lxenlight -lgsl -lgslcblas

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

