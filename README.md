# What is it?
Charon is a reverse proxy HTTP-server, created for academic purposes.

# Cha.. what?
Charon. In Greek mythology, Charon is the ferryman of Hades who carries souls of the newly deceased across the rivers Styx and Acheron that divided the world of the living from the world of the dead (wiki, thank you).

# Reverse proxy? What is it? Where it is used? Is there alternatives?
Reverse proxy is a type of proxy server, that retrieves data from a client, pass it to a backend (for example, Apache), read response and sends it back to client. That technique can be useful in many situations. For example, if backend is synchronous i.e. can't work while not retrieved information from socket, it is possible to perform DoS attack: connect many slow clients so that "fast" clients will be waiting for end of slow connections. Charon is asynchronous, so slow clients will not affect average response time, but when whole HTTP request is accumulated on server, Charon will upstream request to backend. Cause usually reverse proxies and backend is connected using LAN, request will be received on backend very quickly. Also, reverse proxies are often used in web projects as a load balancer: if you have many backend, you need to spread clients over them. And the last commonly used optimization is serving rarely modified static files — CSS, JavaScript and etc. You don't need a backend server to pass them to clients. They can be slower, that proxies, cause they are often written in high-level language. Instead, this work can be entrusted to reverse proxies.
Charon is not the only one HTTP server and reverse proxy in the world. At least there is:
* apache — the most popular HTTP server
* nginx — very popular reverse proxy and HTTP server, often used in big companies as load balancer and static files server
* Varnish — popular caching server and reverse proxy, commonly used to cache whole HTTP response in order to don't hit backend
* Squid — popular caching server and forward proxy

# How can I build Charon?
It's very simple:
```
$ mkdir build
$ cd build
$ cmake .. -DCMAKE_BUILD_TYPE=RELEASE
$ make
```

# How can I run it?
If compilation was successful, you can find an executable **charon** in build directroy. To run Charon, you need pass configuration file via `-c` option and port to bind on. Example configuration files can be found in `conf` directory.
Example:
```
$ ./charon -c ../conf/main.conf 8080
```

# Which features does Charon have?
Charon is still in development and can't to anything complicated now. But it can:
* parse HTTP requests
* form HTTP responses (200 and 404)
* serve static files (currently it's not possible to define root directory, but this functionality will be added soon)

# How does it work?
Charon uses single-threaded event-driven architecture. Socket I/O events is processed using `epoll`. When client connects, worker accepts him and calling HTTP handler initialization routine on created connection (source code can be found in `handlers/http_handler.c`. Everytime somebody sends something, `EPOLLIN` event fired, then event loop passes control to function, specified in `on_request` field in `connection_t`. Next, HTTP handler is doing some parsing work by calling `http_parser_feed`. HTTP parser is implemented using DFA, it's source code can be found in `src/http_parser.c`. After headers parsed, HTTP handler overrides `on_request` field and makes connection eligible to recieve HTTP body buffer. After it's received, HTTP handler forms server response using several buffers, picks them into `outgoing chain` and fires write event on socket, which will be processed by event loop soon.
