CXXFLAGS += -I./
CXXFLAGS += -std=c++11 -Wall -g -c -o

LIB_FILES := -levent -lglog -lgflags -L/usr/local/lib -lgtest -lgtest_main -lpthread

#CPP_SOURCES :=  \
	./base/critical_section.cc \
	./base/location.cc \
	./base/timeutils.cc \
	\
	\
	./net/ip_address.cc \
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
	./net/async_resolver_interface.cc \
	./net/net_helpers.cc \
	./net/physical_socket_server.cc \
	./net/network_monitor.cc \
	./net/checks.cc \
	./net/logging.cc \
	./net/stream.cc \
	./net/socket_stream.cc \
	./net/string_encode.cc \
	./net/base64.cc \
	./net/md5.cc \
	./net/md5_digest.cc \
	./net/sha1.cc \
	./net/sha1_digest.cc \
	./net/string_utils.cc \
	./net/message_digest.cc \
	./net/socket_pool.cc \
	\
	\
	./http/http_common.cc \
	./http/http_base.cc \
	./http/http_server.cc \
	\
	\
	\
	./fb/event_base.cc \
	./fb/request.cc \
	./fb/event_handler.cc \
	./fb/async_timeout.cc \
	\
	\
	./fb/test/socket_pair.cc \
	./fb/test/time_util.cc \

CPP_SOURCES := ./fb/event_base.cc \
	./fb/request.cc \
	./fb/event_handler.cc \
	./fb/async_timeout.cc \
	./fb/async_socket.cc \
	./fb/shutdown_socket_set.cc \
	./fb/io_buffer.cc \
	\
	\
	./net/ip_address.cc \
	./net/socket_address.cc \
	\
	\
	./fb/test/socket_pair.cc \
	./fb/test/time_util.cc \


CPP_OBJECTS := $(CPP_SOURCES:.cc=.o)


#TESTS := ./base/once_unittest \
	./base/thread_unittest \
	./base/worker_thread_unittest \

#TESTS := \
	./base/stl_util_unittest \
	./net/message_queue_unittest \
	./http/http_server_unittest \
	./fb/rw_spin_lock_unittest \
	./fb/scope_guard_unittest \
	./fb/small_locks_unittest \
	./fb/spin_lock_unittest \
	\
	./fb/event_base_unittest \

TESTS := ./fb/rw_spin_lock_unittest \
	./fb/scope_guard_unittest \
	./fb/small_locks_unittest \
	./fb/spin_lock_unittest \
	./fb/event_base_unittest \
	./fb/thread_local_unittest \
	./fb/io_buffer_unittest \

all: $(CPP_OBJECTS) $(TESTS)
.cc.o:
	$(CXX) $(CXXFLAGS) $@ $<

./base/stl_util_unittest : ./base/stl_util_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./base/stl_util_unittest.o: ./base/stl_util_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

./net/message_queue_unittest: ./net/message_queue_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./net/message_queue_unittest.o: ./net/message_queue_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

./http/http_server_unittest: ./http/http_server_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./http/http_server_unittest.o: ./http/http_server_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

################################### fb

./fb/rw_spin_lock_unittest: ./fb/rw_spin_lock_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/rw_spin_lock_unittest.o: ./fb/rw_spin_lock_unittest.cc
	$(CXX) $(CXXFLAGS) $@ $<

./fb/scope_guard_unittest : ./fb/scope_guard_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/scope_guard_unittest.o: ./fb/scope_guard_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

./fb/small_locks_unittest: ./fb/small_locks_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/small_locks_unittest.o: ./fb/small_locks_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

./fb/spin_lock_unittest: ./fb/spin_lock_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/spin_lock_unittest.o: ./fb/spin_lock_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

./fb/event_base_unittest: ./fb/event_base_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/event_base_unittest.o: ./fb/event_base_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

./fb/thread_local_unittest: ./fb/thread_local_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/thread_local_unittest.o: ./fb/thread_local_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

./fb/io_buffer_unittest: ./fb/io_buffer_unittest.o
	$(CXX) -o $@ $< $(CPP_OBJECTS) $(LIB_FILES)
./fb/io_buffer_unittest.o: ./fb/io_buffer_unittest.cc
	$(CXX) -Wno-unused-variable $(CXXFLAGS) $@ $<

clean:
	rm -fr base/*.o
	rm -fr crypto/*.o
	rm -fr net/*.o
	rm -fr http/*.o
	rm -fr fb/*.o
	rm -fr $(TESTS)
