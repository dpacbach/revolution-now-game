CXXFLAGS += -std=c++1z

sdl_cflags  := $(shell sdl2-config --cflags)
sdl_ldflags := $(shell sdl2-config --libs)

CFLAGS  += $(sdl_cflags)
LDFLAGS += $(sdl_ldflags)

main_is := MAIN

$(call enter,src)
