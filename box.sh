#!/bin/sh
set -e

PROJECTROOT="$(dirname $0)"
REMOTEADDR=root@localhost
REMOTEPORT=3022
SSHOPT="-Tp $REMOTEPORT -o BatchMode=yes -i $HOME/.ssh/vm"

function remote_exec()
{
	if [ x$1 != x ]; then
		local action=$1
		cat "$PROJECTROOT/remote/"{config,$action}".sh" | \
		ssh $SSHOPT $REMOTEADDR
	fi
}

function main()
{
	local action=$1

	case "$action" in
		'update')
			# update xen folder
			source ./remote/config.sh
			rsync --progress  -luptrv --rsh="ssh $SSHOPT" \
				"$PROJECTROOT/xen/" "$REMOTEADDR":"$REMOTEDIR/xen"

			# update remote tools folder
			rsync --progress  --del -lptrv --rsh="ssh $SSHOPT" \
				"$PROJECTROOT/remote/" "$REMOTEADDR":"$REMOTEDIR/remote"
			;;
		'configure'|'distclean'|'build')
			remote_exec "$action"
			;;
		'*')
			echo "$action not supported"
			exit 1
			;;
	esac
}

if [ x$1 != x ]; then
	main $1
else
	echo "please specify action"
fi
