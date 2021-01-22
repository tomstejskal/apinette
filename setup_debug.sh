#!/usr/bin/env sh

meson setup --buildtype=debug build -Db_sanitize=address,undefined
