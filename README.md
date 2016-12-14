# What is it?
Charon is a reverse proxy HTTP-server, created for academic purposes.

# Cha.. what?
Charon. In Greek mythology, Charon is the ferryman of Hades who carries souls of the newly deceased across the rivers Styx and Acheron that divided the world of the living from the world of the dead (wiki, thank you).

# Reverse proxy? What is it?
Reverse proxy is a type of proxy server, that retrieves data from a slow client, pass it to a backend (for example, Apache), read response and sends it back to client. Reverse proxy are often used in web-projects as a load-balancer. Also, it can serve static files in order to don't hit "heavy" backend.

# How does it work?
Now Charon uses single-threaded event-driven architecture. Firstly, Charon creates an **epoll** instance to handle I/O events. When client connects and sends a data, server accepts him and feed the data to an **http parser**. After data processing, all parsed HTTP-requests are added to the **request queue**, then Charon pass client connection in a **handler's** hands. He can do anything with client connection, for example, close it or even abort. If handler can process HTTP-requests, which are not completely parsed, it's called *asynchronous*, otherwise -- *synchronous*. To write something to client, handler needs to create an **outgoing chain**, which consist of buffers, located in memory or in file. When client's socket is ready to write, Charon will send data, according to the outgoing chain.

# Ok, got it. How can I build Charon?
It's very simple:
```
# mkdir build
# cd build
# cmake ..
# make
```

# And how can I run it?
If compilation was successful, you can find an executable **charond**. Just run it.

# TODO
* fix all TODOs mentioned in code
* simple http-handler + proxy
* configuration file
* timers (+red-black tree to store them)
* generalized event system
