PROG=set_arinc653
SRC=set_arinc653/$(PROG).c

all: build init-doms

.PHONY: build init-doms test

build:
	$(CC) -o $(PROG) $(SRC) -lxenctrl -luuid -lxenlight
	
/tmp/init.lock:
	for config in /etc/xen/*.cfg; do \
		xl create $$config pool=\"Pool-0\" ; \
	done; \
	./set_arinc653/set_arinc653 \
	touch /tmp/init.lock

init-doms: /tmp/init.lock

test: build init-doms update-doms-client
	@echo "testing"
	xl sched-arinc653 -d base-2 -p base-1
