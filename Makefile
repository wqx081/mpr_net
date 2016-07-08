CXXFLAGS += -I./
CXXFLAGS += -std=c++11 -Wall -g -c -o

LIB_FILES := -lglog -L/usr/local/lib -lgtest -lgtest_main -lpthread

CPP_SOURCES := ./base/once.cc \
	./base/mutex.cc \
	./base/condition_variable.cc \
	./base/semaphore.cc \
	./base/time.cc \
	./base/thread.cc \
	./base/task_queue.cc \
	./base/worker_thread.cc \
	./base/critical_section.cc \
	./base/location.cc \
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

CPP_OBJECTS := $(CPP_SOURCES:.cc=.o)


TESTS := ./base/once_unittest \
	./base/thread_unittest \
	./base/worker_thread_unittest \


all: $(CPP_OBJECTS) $(TESTS)
.cc.o:
	$(CXX) $(CXXFLAGS) $@ $<

./base/once_unittest: ./base/once_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./base/once_unittest.o: ./base/once_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

./base/thread_unittest: ./base/thread_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./base/thread_unittest.o: ./base/thread_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

./base/worker_thread_unittest: ./base/worker_thread_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./base/worker_thread_unittest.o: ./base/worker_thread_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

clean:
	rm -fr base/*.o
	rm -fr crypto/*.o
	rm -fr net/*.o
	rm -fr $(TESTS)
