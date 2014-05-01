CC = gcc
CXX = g++
AR = ar

ROOTDIR = ..
BUILDDIR = build

ifeq ($(shell uname),Darwin)
     PLATFORM_MAC = 1
endif

ifdef PLATFORM_MAC
    DLIB_EXT = dylib
    DLIB_PATH = DYLD_LIBRARY_PATH
else
    DLIB_EXT = so
    DLIB_PATH = LD_LIBRARY_PATH
endif

CFLAGS = -Wall -Werror -Wunused-parameter -Wshadow -Wconversion -Wno-overlength-strings
ifdef DEBUG
    CFLAGS += -g
    LDFLAGS += -g
else
    CFLAGS += -O3
endif

ifdef JSON_NO_PARSER
	CFLAGS += -D JSON_NO_PARSER
endif

ifdef JSON_NO_WRITER
	CFLAGS += -D JSON_NO_WRITER
endif

CFLAGS += -I$(ROOTDIR) -pedantic
ifdef ANSI
    CFLAGS += -ansi
endif

CXXFLAGS = $(CFLAGS)

# Default

.PHONY : default
default : clean run

# Clean

.PHONY : clean
clean :
	rm -r -f $(BUILDDIR)

# Build dynamic library

$(BUILDDIR)/libjsonsax.$(DLIB_EXT) : $(BUILDDIR)/jsonsax.o
	$(CC) $(LDFLAGS) -shared $^ -o $@

$(BUILDDIR)/jsonsax.o : $(ROOTDIR)/jsonsax.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Build static library

$(BUILDDIR)/libjsonsax.a : $(BUILDDIR)/jsonsax_static.o
	$(AR) rcs $@ $^

$(BUILDDIR)/jsonsax_static.o : $(ROOTDIR)/jsonsax.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -D JSON_STATIC -c $< -o $@

# Build dynamically-linked C tests

$(BUILDDIR)/jsonsaxtest : $(BUILDDIR)/jsonsaxtest.o $(BUILDDIR)/libjsonsax.$(DLIB_EXT)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest.o : jsonsaxtest.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build statically-linked C tests

$(BUILDDIR)/jsonsaxtest_static : $(BUILDDIR)/jsonsaxtest_static.o $(BUILDDIR)/libjsonsax.a
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_static.o : jsonsaxtest.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -D JSONSAX_STATIC -c $< -o $@

# Build dynamically-linked C++ tests

$(BUILDDIR)/jsonsaxtest_cpp : $(BUILDDIR)/jsonsaxtest_cpp.o $(BUILDDIR)/libjsonsax.$(DLIB_EXT)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_cpp.o : jsonsaxtest.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

# Build statically-linked C++ tests

$(BUILDDIR)/jsonsaxtest_static_cpp : $(BUILDDIR)/jsonsaxtest_static_cpp.o $(BUILDDIR)/libjsonsax.a
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_static_cpp.o : jsonsaxtest.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -D JSONSAX_STATIC -x c++ -c $< -o $@

# Build all tests

.PHONY : build
build : $(BUILDDIR)/jsonsaxtest $(BUILDDIR)/jsonsaxtest_static $(BUILDDIR)/jsonsaxtest_cpp $(BUILDDIR)/jsonsaxtest_static_cpp

# Run tests

.PHONY : run
run : build
	echo "Testing dynamically-linked library ..."
	$(DLIB_PATH)=$(BUILDDIR) $(BUILDDIR)/jsonsaxtest
	echo "Testing statically-linked library ..."
	$(BUILDDIR)/jsonsaxtest_static
	echo "Testing dynamically-linked library from C++..."
	$(DLIB_PATH)=$(BUILDDIR) $(BUILDDIR)/jsonsaxtest_cpp
	echo "Testing statically-linked library from C++ ..."
	$(BUILDDIR)/jsonsaxtest_static_cpp
