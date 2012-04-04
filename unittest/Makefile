ROOTDIR = ..
include $(ROOTDIR)/Makefile.header

.PHONY : default
default : run_tests

# Build dynamically-linked tests

$(BUILDDIR)/jsonsaxtest : $(BUILDDIR)/jsonsaxtest.o $(BUILDDIR)/libjsonsax.$(DLIB_EXT)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest.o : jsonsaxtest.c ../jsonsax.h
	$(CC) $(CFLAGS) -c $< -o $@

# Build statically-linked tests

$(BUILDDIR)/jsonsaxtest_static : $(BUILDDIR)/jsonsaxtest_static.o $(BUILDDIR)/libjsonsax.a
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_static.o : jsonsaxtest.c ../jsonsax.h
	$(CC) $(CFLAGS) -D JSONSAX_STATIC -c $< -o $@

# Build dynamically-linked C++ tests

$(BUILDDIR)/jsonsaxtest_cpp : $(BUILDDIR)/jsonsaxtest_cpp.o $(BUILDDIR)/libjsonsax.$(DLIB_EXT)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_cpp.o : jsonsaxtest.c ../jsonsax.h
	$(CXX) $(CXXFLAGS) -x c++ -c $< -o $@

# Build statically-linked C++ tests

$(BUILDDIR)/jsonsaxtest_static_cpp : $(BUILDDIR)/jsonsaxtest_static_cpp.o $(BUILDDIR)/libjsonsax.a
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/jsonsaxtest_static_cpp.o : jsonsaxtest.c ../jsonsax.h
	$(CXX) $(CXXFLAGS) -D JSONSAX_STATIC -x c++ -c $< -o $@

# Build all tests

.PHONY : build_tests
build_tests : $(BUILDDIR)/jsonsaxtest $(BUILDDIR)/jsonsaxtest_static $(BUILDDIR)/jsonsaxtest_cpp $(BUILDDIR)/jsonsaxtest_static_cpp

# Run tests

.PHONY : run_tests
run_tests : build_tests
	echo "Testing dnamically-linked library ..."
	$(DLIB_PATH)=$(BUILDDIR) $(BUILDDIR)/jsonsaxtest
	echo "Testing statically-linked library ..."
	$(BUILDDIR)/jsonsaxtest_static
	echo "Testing dynamically-linked library from C++..."
	$(DLIB_PATH)=$(BUILDDIR) $(BUILDDIR)/jsonsaxtest_cpp
	echo "Testing statically-linked library from C++ ..."
	$(BUILDDIR)/jsonsaxtest_static_cpp
