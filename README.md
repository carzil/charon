# What is it?
Charon is a reverse proxy HTTP-server created for academic purposes.

# Cha.. what?
Charon. In Greek mythology, Charon is the ferryman of Hades who carries souls of the newly deceased across the rivers Styx and Acheron that divided the world of the living from the world of the dead (wiki, thank you).

# How can I build Charon?
```
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_BUILD_TYPE=RELEASE
$ make
```

# How can I run it?
If compilation was successful, you can find an executable **charon** in build directroy. To run Charon, you need pass configuration file via `-c` option and pidfile via `-p`. Example of configuration files can be found in `conf` directory.

```
$ charon -c <path to configuration file> -p <path to pidfile>
```

# How can I install Charon?
Yes. Build it and then in build folder:
```
make install
```

# Where to go next?
Check [Getting started](https://github.com/carzil/charon/wiki/Getting-started) and other pages in [wiki](https://github.com/carzil/charon/wiki).
