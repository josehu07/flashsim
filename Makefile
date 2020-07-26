# Makefile for FlashSim.


CXX=g++
CXXFLAGS=-Wall -Wno-unused-result -I$(SSD_DIR)/ -c -std=c++11 -g
LDFLAGS=

SSD_DIR=SSD
FTL_DIR=FTL
TS_DIR=tests
SA_DIR=standalone

HEADERS_SSD=$(SSD_DIR)/ssd.h
SOURCES_SSD=$(filter-out $(SSD_DIR)/ssd_ftl.cpp, $(wildcard $(SSD_DIR)/ssd_*.cpp)) \
            $(wildcard $(FTL_DIR)/*.cpp)
OBJECTS_SSD=$(patsubst %.cpp,%.o,$(SOURCES_SSD))

SOURCES_TS=$(wildcard $(TS_DIR)/*.cpp)
SOURCES_SA=$(wildcard $(SA_DIR)/*.cpp)
PROGRAMS_TS=$(patsubst $(TS_DIR)/%.cpp,%,$(SOURCES_TS))
PROGRAMS_SA=$(patsubst $(SA_DIR)/%.cpp,%,$(SOURCES_SA))


all: $(PROGRAMS_SA)

tests: $(PROGRAMS_TS)


.cpp.o: $(HEADERS_SSD)
	$(CXX) $(CXXFLAGS) $< -o $@


define program_template_ts
  $1 : $$(TS_DIR)/$1.o $$(OBJECTS_SSD)
	$$(CXX) $$(LDFLAGS) $$< $$(OBJECTS_SSD) -o $$@
endef

$(foreach PROG,$(PROGRAMS_TS),$(eval $(call program_template_ts,$(PROG))))


define program_template_sa
  $1 : $$(SA_DIR)/$1.o $$(OBJECTS_SSD)
	$$(CXX) $$(LDFLAGS) $$< $$(OBJECTS_SSD) -o $$@
endef

$(foreach PROG,$(PROGRAMS_SA),$(eval $(call program_template_sa,$(PROG))))


clean:
	@rm -rf $(SSD_DIR)/*.o $(FTL_DIR)/*.o $(PROGRAMS_TS) $(PROGRAMS_SA)
