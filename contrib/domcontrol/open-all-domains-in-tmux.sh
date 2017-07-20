#!/bin/sh

num_domain=0
for conf in /etc/xen/*.cfg; do
    fname=$(basename $conf)
    num_domain=$(( $num_domain+1 ))
    tmux split-window
    tmux select-layout tiled
    tmux send-key -t $num_domain "xl console ${fname%.cfg}" Enter
done

for num in $(seq 1 $num_domain); do
    tmux swap-pane -t $num
done
