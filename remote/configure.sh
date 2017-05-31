#!/bin/sh

set -e
cd "$REMOTEDIR"/xen
./configure --enable-systemd --enable-githttp --enable-stubdom
