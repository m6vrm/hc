Static site generator in C.

Features
--------

*   Theming without logic in the templates
*   Page configuration inheritance
*   Page, blog and menu generation

Installation
------------

    git clone https://github.com/m6vrm/hc
    cd hc
    make
    sudo make install

NOTE: C99 compatible compiler is required.

Usage
-----

Run hc inside your site directory.

By default input files should be located in the content directory, and output
files will be located in the public directory.

NOTE: Output directory will be removed before generating output files.

Inheritance
-----------

Page configuration fields will be searched on parent pages if they are not found
for the current page. Parent page is defined using the following logic:

*   For some/directory/page.html parent is some/directory/index.html
*   For some/directory/index.html pareint is some/index.html

Example
-------

For reference see example directory inside this repository:

    cd example
    make debug

Contributing
------------

    make format         # format source code
    make clean test     # run tests
    make check          # run static checks
    make clean valgrind # run valgrind
