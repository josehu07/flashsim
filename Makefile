# Makefile for FlashSim.

CXX=g++
CXXFLAGS=-Wall -Wno-unused-result -I$(SSD_DIR)/ -c -std=c++11 -O2
LDFLAGS=

SSD_DIR=SSD
FTL_DIR=FTL
RUN_DIR=run

HEADERS=$(SSD_DIR)/ssd.h
SOURCES_SSD = $(filter-out $(SSD_DIR)/ssd_ftl.cpp, $(wildcard $(SSD_DIR)/ssd_*.cpp))  \
              $(wildcard $(FTL_DIR)/*.cpp)                                            \
              $(SSD_DIR)/SSDSim.cpp
SOURCES_RUN = $(wildcard $(RUN_DIR)/run_*.cpp)

OBJECTS_SSD=$(patsubst %.cpp,%.o,$(SOURCES_SSD))
PROGRAMS=$(patsubst $(RUN_DIR)/run_%.cpp,%,$(SOURCES_RUN))


all: $(PROGRAMS)

.cpp.o: $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@

define PROGRAM_TEMPLATE
  $1 : $$(RUN_DIR)/run_$1.o $$(OBJECTS_SSD)
	$$(CXX) $$(LDFLAGS) $$< $$(OBJECTS_SSD) -o $$@
endef

$(foreach prog,$(PROGRAMS),$(eval $(call PROGRAM_TEMPLATE,$(prog))))

clean:
	@rm -rf $(SSD_DIR)/*.o $(FTL_DIR)/*.o $(RUN_DIR)/*.o $(PROGRAMS)
