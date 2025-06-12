# The Mapper

This repository contains the mapper, which assists in finding things on D-Bus.
There is documentation about it in the [docs repository][architecture].

## Prerequisites

Non-OpenBMC build dependencies are:

- meson/ninja
- boost
- libsystemd
- systemd
- tinyxml2

## Build

`meson build && ninja -C build`

## Run Unit Tests

`meson build && ninja -C build test`

## Clean the repository

`rm -rf build`

[architecture]:
  https://github.com/openbmc/docs/blob/master/architecture/object-mapper.md
