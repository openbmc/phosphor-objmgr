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
