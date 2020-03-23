etrace: etrace.o main.o
	g++ -o etrace etrace.o main.o -lstdc++fs
main.o:
	g++ -c main.cpp
etrace.o:
	g++ -c etrace.cpp
clean:
	rm -f etrace main.o etrace.o

