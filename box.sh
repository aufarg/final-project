#!/bin/sh
set -e

REMOTEADDR=root@localhost
REMOTEPORT=3022
REMOTEDIR=/root/final-project
MAKEOPT="-j6"
SSHOPT="-Tp $REMOTEPORT -o BatchMode=yes -i $HOME/.ssh/vm"

function remote_exec()
{
	if [ x$1 != x ]; then
		ssh $SSHOPT $REMOTEADDR < $1
	fi
}

function main()
{
	local root="$(dirname $0)"
	local action=$1

	case "$action" in
		'update')
			# update xen folder
			rsync   -luptrv --rsh="ssh $SSHOPT" \
				"$root/xen" "$REMOTEADDR":"$REMOTEDIR/xen"

			# update remote tools folder
			rsync   --del -lptrv --rsh="ssh $SSHOPT" \
				"$root/remote" "$REMOTEADDR":"$REMOTEDIR/remote"
			;;
		'configure'|'distclean'|'build')
			remote_exec "$root/remote/$action.sh"
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
