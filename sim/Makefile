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
CXX_SRCS = main_sim_sdl.cpp stubs.cpp photo_frame_runtime.cpp $(SRC_DIR)/web_settings.cpp $(SRC_DIR)/photo_frame.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/weather_icons.cpp
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

$(BUILD)/main_sim_sdl.o: main_sim_sdl.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SDL2_CFLAGS) -c $< -o $@

$(BUILD)/stubs.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/photo_frame.o: $(SRC_DIR)/photo_frame.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/photo_frame_runtime.o: photo_frame_runtime.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/web_settings.o: $(SRC_DIR)/web_settings.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/ui_dashboard.o: $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/weather_icons.o: $(SRC_DIR)/weather_icons.cpp
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

$(BUILD)/calendar_scroll_test.o: calendar_scroll_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/bridge.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/calendar_scroll_test: $(BUILD)/calendar_scroll_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-calendar: $(BUILD)/calendar_scroll_test
	./$(BUILD)/calendar_scroll_test

.PHONY: all clean run test-calendar

$(BUILD)/network_status_test.o: network_status_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/web_sync.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/network_status_test: $(BUILD)/network_status_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-network: $(BUILD)/network_status_test
	./$(BUILD)/network_status_test

.PHONY: test-network

$(BUILD)/stubs_home_test.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Drequest_light_toggle=sim_request_light_toggle -c $< -o $@

$(BUILD)/light_sensor_mapping_test.o: light_sensor_mapping_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/bridge.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/light_sensor_mapping_test: $(BUILD)/light_sensor_mapping_test.o $(LVGL_OBJS) $(BUILD)/stubs_home_test.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-home: $(BUILD)/light_sensor_mapping_test
	./$(BUILD)/light_sensor_mapping_test

.PHONY: test-home

$(BUILD)/stubs_photo_test.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DPHOTO_FRAME_TEST -c $< -o $@

$(BUILD)/photo_frame_test.o: photo_frame_test.cpp $(SRC_DIR)/photo_frame.cpp $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DPHOTO_FRAME_TEST -c $< -o $@

$(BUILD)/photo_frame_test: $(BUILD)/photo_frame_test.o $(LVGL_OBJS) $(BUILD)/stubs_photo_test.o $(BUILD)/web_settings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-photo-frame: $(BUILD)/photo_frame_test
	./$(BUILD)/photo_frame_test

.PHONY: test-photo-frame

test-photo-client:
	./photo_client_test/run.sh

.PHONY: test-photo-client

# Exercise the real OTA check/flash boundary with isolated HTTP and flash stubs.
test-ota:
	./ota_test/run.sh

.PHONY: test-ota

test-settings:
	./settings_test/run.sh

.PHONY: test-settings
