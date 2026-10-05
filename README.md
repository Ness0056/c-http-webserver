# C HTTP Web Server

A lightweight HTTP web server implemented in **C**.

The project focuses on low-level networking, HTTP request handling, response generation, and modular C program structure.

## Features

- HTTP request parsing
- HTTP response generation
- Modular C implementation
- Separation of HTTP, data, and utility logic
- CMake-based build configuration
- Low-level systems programming in C

## Technologies

- C
- HTTP
- Socket / network programming
- CMake
- Linux development environment

## Project Structure

```text
.
├── webserver.c
├── http.c
├── http.h
├── data.c
├── data.h
├── util.c
├── util.h
├── CMakeLists.txt
├── rn.lua
├── tox.ini
└── README.md
```

### `webserver.c`

Contains the main server logic and coordinates the handling of incoming
connections and HTTP communication.

### `http.c` / `http.h`

Contains functionality related to HTTP requests and responses.

### `data.c` / `data.h`

Handles data-related functionality used by the server.

### `util.c` / `util.h`

Contains utility functions shared across the project.

## Building

The project uses **CMake**.

Create a build directory:

```bash
mkdir build
cd build
```

Generate the build files:

```bash
cmake ..
```

Compile the project:

```bash
make
```

## Concepts Demonstrated

This project explores several concepts relevant to systems and network
programming:

- HTTP protocol basics
- Client-server communication
- Request parsing
- Response construction
- Modular C programming
- Header and source file organization
- Build systems with CMake
- Low-level debugging and memory-aware programming

## Academic Context

This project was developed as part of university coursework involving
network programming and systems development.

Course-provided infrastructure and third-party components retain their
original attribution where applicable.
