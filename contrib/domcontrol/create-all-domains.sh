#!/bin/sh

for conf in /etc/xen/*.cfg; do
    xl create "$conf" pool=\"Pool-0\"
done
