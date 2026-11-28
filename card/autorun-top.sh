#!/bin/sh
# SampleBoot on stock firmware 1.4 and later. With Developer Mode on, the stock
# app offers to run this file when the card goes in; it starts SampleBoot from
# the autorun folder next to it. Firmware 1.3.0.12 ignores this file and starts
# the autorun folder by itself.
exec /bin/sh "$(dirname -- "$0")/autorun/autorun.sh"
