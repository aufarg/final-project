#!/bin/sh

for conf in /etc/xen/*.cfg; do
    xl create "$conf" pool=\"arinc653pool\"
done
