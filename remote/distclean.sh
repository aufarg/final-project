#!/bin/sh

set -e
cd "$REMOTEDIR"/xen
make $MAKEOPT distclean
