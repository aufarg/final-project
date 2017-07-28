CFLAGS      = -Wall -O2
LDFLAGS     = -lgsl -lgslcblas -lrt

PROG    = periodic
SRC     = periodic.c
OBJ     = ${SRC:.c=.o}

all: build

%.o: %.c
	${CC} -c ${CFLAGS} $<

build: ${OBJ}
	${CC} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm ${OBJ}

