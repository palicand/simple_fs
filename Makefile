all: main
main: main.o
	g++ main.o -ofs  -g
main.o:
	g++ -c -Wall main.cpp -g
clear:
	rm main.o fs