# Covergrind - a code coverage tool for Linux programs (modified Cachegrind).
The tool reports executed lines, branches, and number of calls to each function; highlights lines of code that never ran. </br>
Framework: open source dynamic binary instrumentation framework Valgrind.

## Installation:

1. Follow the link to install Valgrind: http://valgrind.org/downloads/repository.html

2.
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Copy covergrind/ directory into Valgrind root directory</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Add covergrind/ to TOOLS in Makefile.am</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- Add covergrind/Makefile, covergrind/tests/Makefile, covergrind/docs/Makefile, covergrind/cv_annotate to AC_CONFIG_FILES in configure.ac</br>

3. Run:  
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- ./autogen.sh</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- ./configure</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- (sudo) make install</br>


## Usage:

1. Compile your program with -g flag</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Ex. gcc -g program_name (+ any other optional flags)
2. Run:</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- valgrind --tool=covergrind your_program_executable_name</br>
&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;- cv_annotate covergrind.out.pid absolute_path_to_your_program/your_program_name.c</br>
