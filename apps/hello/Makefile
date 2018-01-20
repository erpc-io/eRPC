all:
	g++ -g -std=c++11 -o server server.cc -I ../../src -L ../../build -lerpc -libverbs -lpthread -lnuma
	g++ -g -std=c++11 -o client client.cc -I ../../src -L ../../build -lerpc -libverbs -lpthread -lnuma
clean:
	rm server client

