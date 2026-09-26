# Cross-compiling cpdd for ARMv7 (e.g. Synology DS216play)

Running cpdd directly on a NAS sidesteps SMB/NFS latency entirely — every
stat, open and read becomes a local filesystem op. For armv7 Synology
units (DS216play and similar Cortex-A9 / STM Monaco hardware) the simplest
path is a statically-linked binary built with a musl cross toolchain.

## One-time toolchain setup

    cd /tmp
    wget https://musl.cc/armv7l-linux-musleabihf-cross.tgz
    tar xf armv7l-linux-musleabihf-cross.tgz
    export PATH=/tmp/armv7l-linux-musleabihf-cross/bin:$PATH

(Drop it somewhere persistent like `~/toolchains/` if you'll reuse it.)

## Build

From the cpdd source tree:

    make clean
    make CC=armv7l-linux-musleabihf-gcc STATIC=1 cpdd

Sanity check the output:

    file ./cpdd
    # ELF 32-bit LSB executable, ARM, EABI5 ..., statically linked

If you want to keep a host build alongside, build in a temp copy of the
tree (`cp -a . /tmp/cpdd-arm && cd /tmp/cpdd-arm && make ...`) and copy
the resulting binary back with a distinguishing name, e.g. `cpdd.armv7`.

## Deploy

    scp cpdd.armv7 admin@nas:/volume1/somewhere/cpdd
    ssh admin@nas chmod +x /volume1/somewhere/cpdd

Then run cpdd over SSH against local volume paths instead of mounting
them over SMB on the workstation.

## ABI notes

DS216play (and other Cortex-A9 Synology models) is `armv7l` with VFPv3
hard-float, hence the `musleabihf` toolchain. If a build ever segfaults
on the device with `Illegal instruction`, the chip is probably soft-float
and you want `armv7l-linux-musleabi-cross` instead.
