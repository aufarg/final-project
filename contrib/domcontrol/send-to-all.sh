#!/bin/sh

comm=""
while [ $# -gt 0 ]; do
    comm="$comm $1 Space "
    shift
done

num_dom=0
for conf in /etc/xen/*.cfg; do
    tmux send-key -t $num_dom $comm Enter
    num_dom=$(( num_dom + 1 ))
done
