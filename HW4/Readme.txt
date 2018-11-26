Compiling source code: 
	To compile the program in a Unix environment, run the following commands 
	(without quotes):
	
	'make -f Makefile.LibDisk'
	'make -f Makefile.LibFS'
  'make'
	
	Make sure that all relevant LibDisk and LibFS files and all test files 
	including main.c, simple-test.c, slow-cat.c, slow-export.c, slow-import.c, 
	slow-ls.c, slow-mkdir.c, slow-rm.c, slow-rmdir.c, and slow-touch.c are 
	included within the directory before compilation.


Running the program:
	Before you run any test programs, you will need  to set the environment 
	variable LD_LIBRARY_PATH so that the system can find and link the two 
	libraries at run-time. Assuming you always run your tests from
	from this same directory, use the command (without quotes):

	'setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:.'