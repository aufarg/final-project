#!/bin/sh

cd "$REMOTEDIR"/xen
./configure --enable-systemd --enable-githttp --enable-stubdom
