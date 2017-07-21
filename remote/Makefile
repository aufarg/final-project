all: init-doms

.PHONY: init-doms

/tmp/init.lock:
	for config in /etc/xen/*.cfg; do \
		xl create $$config pool=\"Pool-0\" ; \
	done; \
	./set_arinc653/set_arinc653 \
	touch /tmp/init.lock

init-doms: /tmp/init.lock
