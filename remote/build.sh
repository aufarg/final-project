#!/bin/sh

cd "$REMOTEDIR"/xen
mkdir -p "$REMOTEDIR"/log
make $MAKEOPT debball
dpkg --install dist/xen-upstream-4.8.2-pre.deb
