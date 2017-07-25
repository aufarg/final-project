CFLAGS      = -Wall -O2
LIB_RT      = -lrt
LDFLAGS     = ${LIB_RT}

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

