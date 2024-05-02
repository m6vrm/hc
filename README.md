Static site generator in C.

Features
========

*   Pages and blog generation
*   Theming without logic inside templates

Installation
============

    $ git clone https://m6v.ru/git/hc
    $ cd hc
    $ make
    $ sudo make install

NOTE: C99 compatible compiler is required.

Usage
=====

Run hc inside your site directory:

    $ hc

By default output files will be located in the public directory.

NOTE: Output directory will be removed before generating site files.

Example
-------

For reference see example directory inside this repository:

    $ cd example
    $ make debug

Contribution
============

    $ make format           # format source code
    $ make clean test       # run tests
    $ make check            # run static checks
    $ make clean valgrind   # run valgrind
