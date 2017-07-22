#!/bin/sh

SCHEDULE_EXEC="$PWD/$(dirname $0)/../../contrib/manual-schedule/manual-schedule"

$SCHEDULE_EXEC "$1"
