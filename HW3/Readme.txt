Compiling source code: 
	To compile the program, type 'make'
	in the unix environment. This will 
	create the executable called 'testmem'

Running the program:
	Before you run 'testmem', you will need
	to set the environment variable, LD_LIBRARY_PATH,
	so that the system can find the libmem.so library 
	at run-time. Assuming you always run 'testmem'
	from this same directory, you can use the command:

	setenv LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:.
