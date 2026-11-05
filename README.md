# HTTP/HTTPS Network Simulator

A graphical Computer Networks project built using C and Raylib 5.5. It demonstrates how HTTP and HTTPS requests travel through different network layers in a simulated environment.

## Features

- HTTP and HTTPS protocol selection
- GET and POST request methods
- Custom host, path and request body
- Simulated TCP, IP and Ethernet encapsulation
- Client-to-server request and server-to-client response flow
- Simulated HTTP responses: 200, 400, 404 and 500
- Visualisation of network-layer stages
- Manual and automatic simulation modes
- Pause, resume, previous, next and reset controls
- Adjustable simulation speed
- Raylib graphical interface

## Technologies Used

- C
- Raylib 5.5
- GCC (MinGW-w64)
- OpenGL

## Project Structure

HTTP_HTTPS_Simulator/
- src/main.c
- .gitignore
- README.md

## Requirements

- Windows
- GCC compiler (MinGW-w64)
- Raylib 5.5 for Windows (MinGW-w64)

## Build and Run

1. Install GCC with MinGW-w64.
2. Download Raylib 5.5 for Windows (MinGW-w64).
3. Extract Raylib into a raylib folder in the project directory.
4. Open PowerShell in the project directory.
5. Compile using:

    gcc ".\src\main.c" -o ".\simulator.exe" -I".\raylib\raylib-5.5_win64_mingw-w64\include" -L".\raylib\raylib-5.5_win64_mingw-w64\lib" -lraylib -lopengl32 -lgdi32 -lwinmm

6. Run using:

    .\simulator.exe

## Note

This project is an educational simulation. It demonstrates networking concepts visually; it does not send real network packets or perform actual TLS encryption.

## Author

Prarthana Agarwal
