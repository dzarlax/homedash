CXX = g++
CC = gcc
CXXFLAGS = -O2 -g -Wall -std=c++17
CFLAGS_C = -O2 -g -Wall -std=c11

# Paths
SRC_DIR = ../src
LVGL_DIR = ../.pio/libdeps/esp32-s3-touch-lcd-7b/lvgl
SDL2_CFLAGS = $(shell sdl2-config --cflags)
SDL2_LIBS = $(shell sdl2-config --libs)

# Includes
INCLUDES = \
	-Iesp_stubs \
	-I$(SRC_DIR) \
	-I$(LVGL_DIR) \
	-I$(LVGL_DIR)/src \
	-DLV_CONF_INCLUDE_SIMPLE \
	-DLV_TICK_PERIOD_MS=2 \
	-DDISPLAY_WIDTH=1024 \
	-DDISPLAY_HEIGHT=600 \
	-DLV_COLOR_16_SWAP=0 \
	-DSIMULATOR=1

BUILD = build

# LVGL C sources
LVGL_SRCS = $(shell find $(LVGL_DIR)/src -name '*.c' | grep -v 'lv_demo\|lv_example\|gpu')
LVGL_OBJS = $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/lvgl/%.o,$(LVGL_SRCS))

# App C++ sources
CXX_SRCS = main_sim.cpp stubs.cpp $(SRC_DIR)/ui_dashboard.cpp
CXX_OBJS = $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(CXX_SRCS)))

# Font + cJSON C sources
FONT_SRCS = $(SRC_DIR)/fonts/font_montserrat_16_cyr.c $(SRC_DIR)/fonts/font_montserrat_24_cyr.c
FONT_OBJS = $(BUILD)/font_montserrat_16_cyr.o $(BUILD)/font_montserrat_24_cyr.o
CJSON_OBJ = $(BUILD)/cJSON.o

TARGET = homedash_sim

all: $(TARGET)

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_C) $(INCLUDES) -c $< -o $@

$(BUILD)/main_sim.o: main_sim.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL2_CFLAGS) -c $< -o $@

$(BUILD)/stubs.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/ui_dashboard.o: $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/font_montserrat_16_cyr.o: $(SRC_DIR)/fonts/font_montserrat_16_cyr.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS_C) $(INCLUDES) -c $< -o $@

$(BUILD)/font_montserrat_24_cyr.o: $(SRC_DIR)/fonts/font_montserrat_24_cyr.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS_C) $(INCLUDES) -c $< -o $@

$(CJSON_OBJ): esp_stubs/cJSON.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS_C) $(INCLUDES) -c $< -o $@

$(TARGET): $(LVGL_OBJS) $(CXX_OBJS) $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ $(SDL2_LIBS) -o $@ -lm -lpthread

clean:
	rm -rf $(BUILD) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
