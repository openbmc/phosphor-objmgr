libdir=/usr/lib
includedir=/usr/include

PACKAGE_DEPS=libsystemd
SONAME=libmapper.so
VERSION=1
LIBMAPPER=$(SONAME).$(VERSION)
INCLUDES=mapper.h

LDLIBS+=$(shell pkg-config --libs $(PACKAGE_DEPS))
ALL_CFLAGS+=$(shell pkg-config --cflags $(PACKAGE_DEPS)) -fPIC -Werror $(CFLAGS)

all: $(SONAME)

%.o: %.c
	$(CC) -c $(ALL_CFLAGS) -o $@ $<

$(SONAME): $(LIBMAPPER)
	ln -sf $^ $@

$(LIBMAPPER): lib%.so.$(VERSION): %.o
	$(CC) -shared $(CFLAGS) $(LDFLAGS) -Wl,-soname,$(SONAME) \
		-o $@ $^ $(LDLIBS)

install: $(SONAME) $(LIBMAPPER)
	@mkdir -p $(DESTDIR)$(includedir)
	install $(INCLUDES) $(DESTDIR)$(includedir)
	@mkdir -p $(DESTDIR)$(libdir)
	install $(LIBMAPPER) $(DESTDIR)$(libdir)
	ln -sf $(LIBMAPPER) $(DESTDIR)$(libdir)/$(SONAME)

clean:
	rm -f *.o $(LIBMAPPER) $(SONAME)
