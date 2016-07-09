CXXFLAGS += -I./
CXXFLAGS += -std=c++11 -Wall -g -c -o

LIB_FILES := -lglog -L/usr/local/lib -lgtest -lgtest_main -lpthread

CPP_SOURCES :=  \
	./base/critical_section.cc \
	./base/location.cc \
	./base/timeutils.cc \
	\
	\
	./net/ipaddress.cc \
	./net/socket_address.cc \
	./net/sigslot.cc \
	./net/async_socket.cc \
	./net/buffer.cc \
	./net/buffer_queue.cc \
	./net/event.cc \
	./net/sharedexclusivelock.cc \
	./net/message_handler.cc \
	./net/message_queue.cc \
	./net/platform_thread.cc \
	./net/thread_checker_impl.cc \
	./net/event_tracer.cc \
	./net/null_socket_server.cc \
	./net/thread.cc \
	./net/signal_thread.cc \
	./net/async_file.cc \
	./net/net_helpers.cc \
	./net/physical_socket_server.cc \
	./net/network_monitor.cc \
	./net/stream.cc \

CPP_OBJECTS := $(CPP_SOURCES:.cc=.o)


#TESTS := ./base/once_unittest \
	./base/thread_unittest \
	./base/worker_thread_unittest \

TESTS := ./net/message_queue_unittest \


all: $(CPP_OBJECTS) $(TESTS)
.cc.o:
	$(CXX) $(CXXFLAGS) $@ $<

./net/message_queue_unittest: ./net/message_queue_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./net/message_queue_unittest.o: ./net/message_queue_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

clean:
	rm -fr base/*.o
	rm -fr crypto/*.o
	rm -fr net/*.o
	rm -fr $(TESTS)
