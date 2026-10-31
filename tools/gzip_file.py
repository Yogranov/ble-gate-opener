#!/usr/bin/env python3
"""Gzips the web page at build time. No timestamp, so builds are repeatable."""
import gzip
import sys

with open(sys.argv[1], "rb") as source:
    data = source.read()
with open(sys.argv[2], "wb") as target:
    target.write(gzip.compress(data, compresslevel=9, mtime=0))
