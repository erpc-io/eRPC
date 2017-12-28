AM_CFLAGS = -g -Wall -Werror -D_GNU_SOURCE -I$(includedir)
LDFLAGS += @NUMA_LIB@
EXTRA_DIST = src/mlx5.map libmlx5.spec.in mlx5.driver
EXTRA_DIST += debian
EXTRA_DIST += autogen.sh
EXTRA_DIST += scripts/expose_libmlx5_headers/libmlx_expose_headers scripts/expose_libmlx5_headers/defines.txt scripts/expose_libmlx5_headers/structures.txt scripts/expose_libmlx5_headers/enumerations.txt
EXTRA_DIST += libmlx5.spec


mlx5_version_script = @MLX5_VERSION_SCRIPT@

MLX5_SOURCES = src/buf.c src/cq.c src/dbrec.c src/mlx5.c src/qp.c src/srq.c src/verbs.c src/implicit_lkey.c src/ec.c
noinst_HEADERS = src/bitmap.h src/doorbell.h src/list.h src/mlx5-abi.h src/mlx5.h src/wqe.h src/implicit_lkey.h src/ec.h src/mlx5dv.h

if HAVE_IBV_DEVICE_LIBRARY_EXTENSION
    lib_LTLIBRARIES = src/libmlx5.la
    src_libmlx5_la_SOURCES = $(MLX5_SOURCES)
    src_libmlx5_la_LDFLAGS = -version-info 1 -export-dynamic \
        $(mlx5_version_script)
    mlx5confdir = $(sysconfdir)/libibverbs.d
    mlx5conf_DATA = mlx5.driver
    src_libmlx5_la_DEPENDENCIES = $(srcdir)/src/mlx5.map
else
    mlx5libdir = $(libdir)/infiniband
    mlx5lib_LTLIBRARIES = src/mlx5.la
    src_mlx5_la_SOURCES = $(MLX5_SOURCES)
    src_mlx5_la_LDFLAGS = -version-info 1 -export-dynamic -module $(mlx5_version_script)
    src_mlx5_la_DEPENDENCIES = $(srcdir)/src/mlx5.map
endif

install-data-hook:
	mkdir -p $(DESTDIR)$(prefix)/include/infiniband
	$(top_srcdir)/scripts/expose_libmlx5_headers/libmlx_expose_headers $(top_srcdir)/scripts/expose_libmlx5_headers/defines.txt $(top_srcdir)/scripts/expose_libmlx5_headers/structures.txt $(top_srcdir)/scripts/expose_libmlx5_headers/enumerations.txt $(DESTDIR)$(prefix)
	cp $(srcdir)/src/mlx5dv.h $(DESTDIR)$(prefix)/include/infiniband/mlx5dv.h
	cd $(DESTDIR)$(libdir) && $(LN_S) -f libmlx5.so.1.0.0 libmlx5-rdmav2.so

uninstall-hook:
	rm -f $(DESTDIR)$(prefix)/include/infiniband/mlx5_hw.h
	rm -f $(DESTDIR)$(prefix)/include/infiniband/mlx5dv.h
	rm -f $(DESTDIR)$(libdir)/libmlx5-rdmav2.so
