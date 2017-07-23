CFLAGS      = -Wall
LIB_GSL     = -lgsl -lgslcblas
LIB_RT      = -lrt
LIB_PTHREAD = -lpthread
LDFLAGS     = ${LIB_GSL} ${LIB_RT} ${LIB_PTHREAD}

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

