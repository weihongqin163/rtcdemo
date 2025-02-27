rm *.o
rm vad
g++ -g -c main.cpp -o main.o
g++ -g -c vad.cpp -o vad.o  
g++ vad.o main.o -o vad
