
# But why

Because

# Requirements

```
~# apt install libglfw3-dev libglew-dev libpng-dev
~# python3 -m pip install gprof2dot
```

# Build

```
~# git submodule update --init
~# make
```

# Run

```
~# ./n64 rom/SomeRom.n64
```

# Bios

The PIF ROM image can be obtained by searching for the MD5 or SHA1:
- IPL 1.0 NTSC:
    + MD5: 4921d5f2165dee6e2496f4388c4c81da
    + SHA1: 9174eadc0f0ea2654c95fd941406ab46b9dc9bdd
