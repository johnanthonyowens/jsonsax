ROOTDIR = .
include $(ROOTDIR)/Makefile.header

.PHONY : default
default : build_dynamic build_static

# Clean

.PHONY : clean
clean :
	rm -r -f $(BUILDDIR)

# Build dynamic library

.PHONY : build_dynamic
build_dynamic : $(BUILDDIR)/libjsonsax.$(DLIB_EXT)

$(BUILDDIR)/libjsonsax.$(DLIB_EXT) : $(BUILDDIR)/jsonsax.o
	$(CC) $(CFLAGS) -shared $^ -o $@

$(BUILDDIR)/jsonsax.o : jsonsax.c jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Build static library

.PHONY : build_static
build_static : $(BUILDDIR)/libjsonsax.a

$(BUILDDIR)/libjsonsax.a : $(BUILDDIR)/jsonsax_static.o
	$(AR) rcs $@ $^

$(BUILDDIR)/jsonsax_static.o : jsonsax.c jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) -D JSON_STATIC -c $< -o $@

# Build and run unit tests

.PHONY : test
test : clean build_dynamic build_static
	$(MAKE) -C unittest
