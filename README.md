Features
========

*   Pages and blog generation
*   Simple theming without logic inside templates

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

Example
-------

For reference see example directory inside this repository:

    $ cd example
    $ make debug

Arguments
---------

    -i      <path>  input dir, default "content"
    -o      <path>  output dir, default "public"
    -t      <path>  theme dir, default "theme"
    -r      <url>   root url, default ""
    -p      <path>  create or edit page at <input dir>/<path>
    -b              create or edit blog post
    -v              print version

Contribution
============

    $ make format           # format source code
    $ make clean test       # run tests
    $ make check            # run static checks
    $ make clean valgrind   # run valgrind
