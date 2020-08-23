MODULE_big = jsonb_schema
OBJS = jsonb_schema.o
PGFILEDESC = "jsonb_schema - jsonb with separately stored schema"

EXTENSION = jsonb_schema
DATA = jsonb_schema--1.0.sql

REGRESS = test

ifdef USE_PGXS
PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/jsonb_schema
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
