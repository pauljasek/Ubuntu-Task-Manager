all:
	cd systemcall; make
	cd app; make

clean:
	cd systemcall; make clean
	cd app; make clean
