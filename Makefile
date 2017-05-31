REMOTESCRIPT=./box.sh
VMNAME=final-project

.PHONY: all update startvm restartvm distclean configure world

all: build restartvm

update: startvm
	@$(REMOTESCRIPT) update

build: update
	@$(REMOTESCRIPT) build

world: | reset all

reset: startvm
	@$(REMOTESCRIPT) distclean
	@$(REMOTESCRIPT) configure

startvm:
	$(shell vboxmanage showvminfo $(VMNAME) | grep -q "running")
	@if [ $(.SHELLSTATUS) -ne 0 ]; then \
		vboxmanage startvm --type headless $(VMNAME); \
	fi;

restartvm:
	$(shell vboxmanage showvminfo $(VMNAME) | grep -q "running")
	@if [ $(.SHELLSTATUS) -eq 0 ]; then \
		vboxmanage controlvm $(VMNAME) reset; \
	fi;
