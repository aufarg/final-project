PROG=set_arinc653
SRC=set_arinc653/$(PROG).c

all: build setup

build:
	$(CC) -o $(PROG) $(SRC) -lxenctrl -luuid -lxenlight
	
init-doms:
	@if [ ! -f /tmp/init.lock]; then \
		for config in /etc/xen/*.cfg; do \
			xl create $$config pool=\"Pool-0\" ; \
		done; \
		./set_arinc653/set_arinc653 \
	fi;
	touch /tmp/init.lock

test: build init-doms
	xl sched-arinc653 -d base-2 -p base-1
	@echo "testing"
