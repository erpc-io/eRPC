#Notes:
#1. The make target depends on how eRPC was compiled:
#   * If DTRANSPORT=dpdk, use `make dpdk`
#   * If DTRANSPORT=raw, use `make raw`
#   * If DTRANSPORT=infiniband, use `make infiniband`
LIBS = -lerpc -lpthread -lnuma -ldl

error:
	@echo "Please choose one of the following targets: infiniband, raw, dpdk, clean"
	@exit 2
infiniband:
	g++ -g -std=c++11 -o server server.cc -I ../src -I ../third_party/asio/include -L ../build $(LIBS) -libverbs -DERPC_INFINIBAND=true
	g++ -g -std=c++11 -o client client.cc -I ../src -I ../third_party/asio/include -L ../build $(LIBS) -libverbs -DERPC_INFINIBAND=true
raw:
	g++ -g -std=c++11 -o server server.cc -I ../src -I ../third_party/asio/include -L ../build $(LIBS) -libverbs -DERPC_RAW=true
	g++ -g -std=c++11 -o client client.cc -I ../src -I ../third_party/asio/include -L ../build $(LIBS) -libverbs -DERPC_RAW=true
dpdk:
	g++ -g -std=c++11 -o server server.cc \
		-Wno-deprecated-declarations \
		-DERPC_DPDK=true -march=native \
		-I ../src -I ../third_party/asio/include -I /usr/include/dpdk \
		-L ../build $(LIBS) \
		-Wl,--whole-archive -ldpdk -Wl,--no-whole-archive \
		-lnuma -ldl -libverbs -lmlx4 -lmlx5

	g++ -g -std=c++11 -o client client.cc \
		-Wno-deprecated-declarations \
		-DERPC_DPDK=true -march=native \
		-I ../src -I ../third_party/asio/include -I /usr/include/dpdk \
		-L ../build $(LIBS) \
		-Wl,--whole-archive -ldpdk -Wl,--no-whole-archive \
		-lnuma -ldl -libverbs -lmlx4 -lmlx5
clean:
	rm server client

