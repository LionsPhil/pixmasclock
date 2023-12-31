# Phil's Universal Makefile; configured for pixmas (and stripped down a bit)
# BUILD CONFIGURATION ---------------------------------------------------------
  BINARY = pixmas
 VERSION = 0.1
 SCRATCH = /tmp/$(BINARY)-scratch/
DISTFILE = $(BINARY)-$(VERSION).zip
 DISTMSG = "PiXmas $(VERSION)"

# Expects GNU-flavoured tools, except for Clang in preference to GCC
  CPPC = clang++
    LD = clang++
   ZIP = zip
 UNZIP = unzip
    SH = sh
  ECHO = echo
PRINTF = printf
    RM = rm
 MKDIR = mkdir
    CP = cp
    MV = mv
 EGREP = egrep
   GDB = gdb

# Extra flags to control build type
# Debugging:
  CFLAGSEX = -g -O
CPPFLAGSEX = $(CFLAGSEX)
 LDFLAGSEX =
# Release:
#  CFLAGSEX = -O3 -DNDEBUG
#CPPFLAGSEX = $(CFLAGSEX)
# LDFLAGSEX =

ifndef SDLVERSION
	SDLVERSION=2
endif

# SOURCES ---------------------------------------------------------------------
# Main changes based on SDL version so is inferred below.
CPPSOURCES = snowfp.cpp snowint.cpp snowclock.cpp popclock.cpp colorcycle.cpp \
            digitalclock.cpp
   HEADERS = hack.hpp digitalclock.hpp
# Anything else you want put in the distributed version
 EXTRADIST = Makefile README.md

# PREAMBLE / AUTOCONFIGURATION / DERIVED --------------------------------------
OBJECTS = $(CPPSOURCES:%.cpp=%.o)
NOTOBJECTS = $(filter-out %.o, $(OBJECTS))
ifneq ($(NOTOBJECTS),)
	$(error OBJECTS contains non-object(s) $(NOTOBJECTS))
endif
SOURCES = $(CPPSOURCES)

ifeq ($(SDLVERSION),1)
	PKGCONFIGPKGS = sdl
	CPPSOURCES += pixmas.cpp
else
	PKGCONFIGPKGS += sdl2 SDL2_ttf libconfuse
	CPPSOURCES += pixmas2.cpp menu.cpp
endif

# All files which are sources, /including/ non-compiled ones (e.g. headers)
ALLSOURCESMANU = $(SOURCES) $(HEADERS)

# Compiler warning flags
WARNFLAGS = -Werror -W -Wall -Wpointer-arith -Wcast-align -Wwrite-strings \
            -Wno-unused-parameter -Wuninitialized
CPPWFLAGS = $(WARNFLAGS)

# Tool flags
# Don't make CXXFLAGS include CFLAGS or it'll get duplicate CFLAGSEX
CPPFLAGS  = $(CPPWFLAGS) -std=c++14 -pedantic -DVERSION='"$(VERSION)"' \
            `pkg-config $(PKGCONFIGPKGS) --cflags` \
			-DSDLVERSION='$(SDLVERSION)' $(CPPFLAGSEX)
LDFLAGS   = `pkg-config $(PKGCONFIGPKGS) --libs` -lm $(LDFLAGSEX)

EXTRACDEPS = Makefile $(HEADERS)

# Kinda sloppy autodetection that we should fake the display resolution.
ifeq ($(shell uname -m),x86_64)
	CPPFLAGS += -DDESKTOP
endif

# MAKEFILE METADATA AND MISCELLANY --------------------------------------------
# Vpath is a colon separated list of source directories
VPATH = src

# Gratuitous colours. While they make your build process look like an
# '80s disco, they also make it easy to see what it's doing at-a-glance.
RED=\033[31;1m
YELLOW=\033[33;1m
GREEN=\033[32;1m
BLUE=\033[34;1m
MAGENTA=\033[35;1m
CYAN=\033[36;1m
WHITE=\033[0m
RV=\033[7m
COLUMN2 = \033[40G

# Phony targets - these produce no output files (and are not files themselves)
.PHONY: all clean dist disttest work env info run runonpi

# RULES =======================================================================
all: $(BINARY)

$(BINARY): $(OBJECTS)
	@$(PRINTF) "$(BLUE)--- $(RV)LINKING   $(WHITE) $@\n"
	@$(LD) -o $@ $^ $(LDFLAGS)
	@$(PRINTF) "$(BLUE)$(RV)***$(WHITE) $(BINARY) built\n"

%.o : %.cpp $(EXTRACDEPS)
	@$(PRINTF) "$(GREEN)--- $(RV)COMPILING $(WHITE) $<\n"
	@$(CPPC) -o $@ -c $(CPPFLAGS) $<

clean:
	@$(PRINTF) "$(RED)--- $(RV)CLEANING  $(WHITE)\n"
	@$(RM) -fv  $(OBJECTS)
	@$(RM) -frv $(SCRATCH)
	@$(RM) -fv $(BINARY) $(DISTFILE) $(DEFFILE)
	@$(PRINTF) "$(RED)$(RV)***$(WHITE) Cleansed\n"

# Create distributable archive
dist: $(DISTFILE)
$(DISTFILE): $(SOURCES) $(HEADERS) $(EXTRADIST)
	@$(PRINTF) "$(MAGENTA)--- $(RV)BUNDLING  $(WHITE) $(DISTFILE)\n"
	@$(ECHO) $(DISTMSG) | $(ZIP) -z -9 $(DISTFILE) $^
	@$(PRINTF) "$(MAGENTA)$(RV)***$(WHITE) $(DISTFILE) created\n"

# Test distributable
disttest: $(DISTFILE)
	@$(PRINTF) "$(CYAN)--- $(RV)TESTING   $(WHITE) $(DISTFILE)\n"
	@$(MKDIR) -p $(SCRATCH)
	@$(CP) $(DISTFILE) $(SCRATCH)
	@$(ECHO) "cd $(SCRATCH); $(UNZIP) $(DISTFILE); make" | $(SH)
	@$(PRINTF) "$(CYAN)$(RV)***$(WHITE) Distributable tested\n"

# To `make work' is to find things to do ;)
work: $(SOURCES) $(HEADERS)
	@$(PRINTF) "$(RV)***$(WHITE) Outstanding tasks:\n"
	@$(EGREP) '(\\todo|TODO|FIXME)' --color=auto $^ || true

# Build environment information
env:
	@$(ECHO) "pkg-config pkgs. : $(PKGCONFIGPKGS)"
	@$(ECHO) "C++ compiler     : $(CPPC)"
	@$(ECHO) "Linker           : $(LD)"
	@$(ECHO) "C++ compile flags: $(CPPFLAGS)"
	@$(ECHO) "Linker flags     : $(LDFLAGS)"

# Complete Makefile information
info: env
	@$(ECHO) "C++ sources      : $(CPPSOURCES)"
	@$(ECHO) "Objects          : $(OBJECTS)"

run: all
	@$(PRINTF) "$(WHITE)--- $(RV)EXECUTING $(WHITE) $(BINARY)\n"
	@./$(BINARY)

debug: all
	@$(PRINTF) "$(WHITE)--- $(RV)DEBUGGING $(WHITE) $(BINARY)\n"
	@$(GDB) -ex run ./$(BINARY)

runonpi: all
	sudo SDL_VIDEODRIVER="fbcon" SDL_FBDEV="/dev/fb1" ./pixmas
