CC = gcc

ROOTDIR = ..
BUILDDIR = build

CFLAGS = -Wall -Werror -Wunused-parameter -Wshadow
ifdef DEBUG
    CFLAGS += -g
    LDFLAGS += -g
else
    CFLAGS += -Os
endif

CFLAGS += -I$(ROOTDIR) -pedantic
ifdef ANSI
    CFLAGS += -ansi
endif

# Default

.PHONY : default
default : build

# Clean

.PHONY : clean
clean :
	rm -r -f $(BUILDDIR)

# Build

.PHONY : build
build : $(BUILDDIR)/pj

$(BUILDDIR)/pj : $(BUILDDIR)/pj.o $(BUILDDIR)/jsonsax.o
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILDDIR)/pj.o : pj.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -D JSONSAX_STATIC -c $< -o $@

$(BUILDDIR)/jsonsax.o : $(ROOTDIR)/jsonsax.c $(ROOTDIR)/jsonsax.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@
