#!/bin/bash

rm -f seed-* peer-*

pkill -f peer
pkill -f "^./seed "
echo "Cleanup complete."
