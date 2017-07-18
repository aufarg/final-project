CFLAGS      = -Wall
LIB_GSL     = -lgsl -lgslcblas
LDFLAGS     = ${LIB_GSL}

PROG    = tms
SRC     = tms.c
OBJ     = ${SRC:.c=.o}

all: build

%.o: %.c
	${CC} -c ${CFLAGS} $<

build: ${OBJ}
	${CC} -o ${PROG} ${OBJ} ${LDFLAGS}

clean:
	rm ${OBJ}

