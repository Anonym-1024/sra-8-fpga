#!/bin/bash

set -e
ylangc example/echo.y
ylangc example/string.y
sra8-as example/echo.s
sra8-as example/string.s
sra8-ld --format=mem -T example/boot.ld -o program.mem example/uart.o example/echo.o example/string.o
make clean
make prog
