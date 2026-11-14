CXX = g++
CC = gcc
CXXFLAGS = -O2 -g -Wall -std=c++17 -MMD -MP
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
CXX_SRCS = main_sim_sdl.cpp stubs.cpp photo_frame_runtime.cpp $(SRC_DIR)/web_settings.cpp $(SRC_DIR)/photo_frame.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/ui_strings.cpp $(SRC_DIR)/weather_icons.cpp
CXX_OBJS = $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(CXX_SRCS)))

# Font + cJSON C sources
FONT_SRCS = $(SRC_DIR)/fonts/font_montserrat_16_cyr.c $(SRC_DIR)/fonts/font_montserrat_24_cyr.c
FONT_OBJS = $(BUILD)/font_montserrat_16_cyr.o $(BUILD)/font_montserrat_24_cyr.o $(BUILD)/font_photo_clock_80.o
CJSON_OBJ = $(BUILD)/cJSON.o

TARGET = homedash_sim

all: $(TARGET)

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c $(SRC_DIR)/lv_conf.h
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

$(BUILD)/ui_strings.o: $(SRC_DIR)/ui_strings.cpp
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

$(BUILD)/font_photo_clock_80.o: $(SRC_DIR)/fonts/font_photo_clock_80.c
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

$(BUILD)/calendar_scroll_test: $(BUILD)/calendar_scroll_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-calendar: $(BUILD)/calendar_scroll_test
	./$(BUILD)/calendar_scroll_test

.PHONY: all clean run test-calendar

$(BUILD)/network_status_test.o: network_status_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/web_sync.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/network_status_test: $(BUILD)/network_status_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-network: $(BUILD)/network_status_test
	./$(BUILD)/network_status_test

.PHONY: test-network

$(BUILD)/stubs_home_test.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Drequest_light_toggle=sim_request_light_toggle -Drequest_home_action=sim_request_home_action -c $< -o $@

$(BUILD)/light_sensor_mapping_test.o: light_sensor_mapping_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/bridge.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/light_sensor_mapping_test: $(BUILD)/light_sensor_mapping_test.o $(LVGL_OBJS) $(BUILD)/stubs_home_test.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
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

$(BUILD)/photo_frame_test: $(BUILD)/photo_frame_test.o $(LVGL_OBJS) $(BUILD)/stubs_photo_test.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
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

test-localization: $(TARGET)
	HOMEDASH_SIM_LOCALE_TEST=1 SDL_VIDEODRIVER=dummy ./$(TARGET)
	HOMEDASH_SIM_LOCALE_TEST=1 HOMEDASH_SIM_PHOTO_FRAME=1 HOMEDASH_SIM_PHOTO_MODE_TEST=1 SDL_VIDEODRIVER=dummy ./$(TARGET)

.PHONY: test-localization

$(BUILD)/stubs_settings_tab.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DPHOTO_FRAME_TEST -Drequest_recovery_ap=sim_request_recovery_ap -Drequest_stop_recovery_ap=sim_request_stop_recovery_ap -c $< -o $@

$(BUILD)/settings_tab_test.o: settings_tab_test.cpp $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/settings_tab_test: $(BUILD)/settings_tab_test.o $(LVGL_OBJS) $(BUILD)/stubs_settings_tab.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-settings-tab: $(BUILD)/settings_tab_test
	./$(BUILD)/settings_tab_test

.PHONY: test-settings-tab

test-action-feedback: test-action-feedback-ui test-action-executor
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) action_feedback_test.cpp $(SRC_DIR)/action_feedback.cpp -o $(BUILD)/action_feedback_test
	$(BUILD)/action_feedback_test

test-action-bridge:
	sh ./action_bridge_test/run.sh

$(BUILD)/action_feedback_ui_test.o: action_feedback_ui_test.cpp $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/action_feedback_ui_test: $(BUILD)/action_feedback_ui_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-action-feedback-ui: $(BUILD)/action_feedback_ui_test
	./$(BUILD)/action_feedback_ui_test

test-action-executor:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) action_executor_test.cpp $(SRC_DIR)/action_feedback.cpp $(SRC_DIR)/action_executor.cpp -o $(BUILD)/action_executor_test
	$(BUILD)/action_executor_test

.PHONY: test-brightness
test-brightness:
	sh brightness_test/run.sh

$(BUILD)/stubs_reference.o: stubs.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DBRIGHTNESS_REFERENCE_UI_TEST -c $< -o $@

$(BUILD)/brightness_reference_ui_test.o: brightness_reference_ui_test.cpp $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/brightness_reference_ui_test: $(BUILD)/brightness_reference_ui_test.o $(LVGL_OBJS) $(BUILD)/stubs_reference.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-brightness-reference-ui: $(BUILD)/brightness_reference_ui_test
	./$(BUILD)/brightness_reference_ui_test

.PHONY: test-brightness-reference-ui

$(BUILD)/day_test.o: day_test.cpp $(SRC_DIR)/ui_dashboard.cpp $(SRC_DIR)/bridge.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/day_test: $(BUILD)/day_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-day: $(BUILD)/day_test
	./$(BUILD)/day_test

.PHONY: test-day

# Track shared structure/header changes in incremental simulator builds.
-include $(wildcard $(BUILD)/*.d)

$(BUILD)/day_night_ui_test.o: day_night_ui_test.cpp $(SRC_DIR)/ui_dashboard.cpp brightness_fixture.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/day_night_ui_test: $(BUILD)/day_night_ui_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

.PHONY: test-day-night
test-day-night: $(BUILD)/day_night_ui_test
	./$(BUILD)/day_night_ui_test

$(BUILD)/summary_test.o: summary_test.cpp $(SRC_DIR)/ui_dashboard.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/summary_test: $(BUILD)/summary_test.o $(LVGL_OBJS) $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

.PHONY: test-summary
test-summary: $(BUILD)/summary_test
	./$(BUILD)/summary_test

$(BUILD)/ui_voice.o: $(SRC_DIR)/ui_voice.cpp $(SRC_DIR)/ui_voice.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/voice_overlay_test.o: voice_overlay_test.cpp $(SRC_DIR)/ui_voice.h $(SRC_DIR)/ui_strings.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/voice_overlay_test: $(BUILD)/voice_overlay_test.o $(BUILD)/ui_voice.o $(BUILD)/ui_strings.o $(FONT_OBJS) $(LVGL_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-voice-overlay: $(BUILD)/voice_overlay_test
	./$(BUILD)/voice_overlay_test

VOICE_FIXTURE ?= fixtures/voice_snapshot.json

test-voice-protocol: $(CJSON_OBJ)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRC_DIR)/voice_protocol.cpp $(SRC_DIR)/voice_fragments.cpp voice_protocol_test.cpp -o $(BUILD)/voice_protocol_test
	./$(BUILD)/voice_protocol_test "$(VOICE_FIXTURE)"
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRC_DIR)/voice_view_model.cpp voice_state_test.cpp -o $(BUILD)/voice_state_test
	./$(BUILD)/voice_state_test
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRC_DIR)/voice_view_model.cpp voice_delivery_state_test.cpp -o $(BUILD)/voice_delivery_state_test
	./$(BUILD)/voice_delivery_state_test
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRC_DIR)/voice_config.cpp voice_config_test.cpp -o $(BUILD)/voice_config_test
	./$(BUILD)/voice_config_test
	$(CXX) $(CXXFLAGS) -Ivoice_nvs_stubs -I$(SRC_DIR) $(SRC_DIR)/voice_config.cpp $(SRC_DIR)/voice_config_store.cpp voice_config_store_test.cpp -o $(BUILD)/voice_config_store_test
	./$(BUILD)/voice_config_store_test
	$(CXX) $(CXXFLAGS) -Iesp_stubs -I$(SRC_DIR) $(SRC_DIR)/voice_config.cpp $(SRC_DIR)/voice_recovery_request.cpp voice_recovery_request_test.cpp $(CJSON_OBJ) -o $(BUILD)/voice_recovery_request_test
	./$(BUILD)/voice_recovery_request_test
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) voice_backoff_test.cpp -o $(BUILD)/voice_backoff_test
	./$(BUILD)/voice_backoff_test

.PHONY: test-voice-overlay test-voice-protocol

$(BUILD)/voice_screenshots.o: voice_screenshots.cpp $(SRC_DIR)/ui_voice.h $(SRC_DIR)/ui_strings.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/voice_screenshots: $(BUILD)/voice_screenshots.o $(BUILD)/ui_voice.o $(BUILD)/ui_strings.o $(FONT_OBJS) $(LVGL_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

$(BUILD)/voice_push_client_sim.o: $(SRC_DIR)/voice_push_client.cpp $(SRC_DIR)/voice_push_client.h $(SRC_DIR)/voice_view_model.h $(SRC_DIR)/voice_protocol.h $(SRC_DIR)/voice_fragments.h $(SRC_DIR)/voice_config.h $(SRC_DIR)/voice_backoff.h $(SRC_DIR)/ui_voice.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_protocol_sim.o: $(SRC_DIR)/voice_protocol.cpp $(SRC_DIR)/voice_protocol.h $(SRC_DIR)/voice_view_model.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_fragments_sim.o: $(SRC_DIR)/voice_fragments.cpp $(SRC_DIR)/voice_fragments.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_config_sim.o: $(SRC_DIR)/voice_config.cpp $(SRC_DIR)/voice_config.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_view_model_sim.o: $(SRC_DIR)/voice_view_model.cpp $(SRC_DIR)/voice_view_model.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_integration_test.o: voice_integration_test.cpp $(SRC_DIR)/voice_push_client.h $(SRC_DIR)/ui_voice.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_integration_test: $(BUILD)/voice_integration_test.o $(BUILD)/voice_push_client_sim.o $(BUILD)/voice_protocol_sim.o $(BUILD)/voice_fragments_sim.o $(BUILD)/voice_view_model_sim.o $(BUILD)/voice_config_sim.o $(BUILD)/ui_voice.o $(BUILD)/ui_dashboard.o $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ) $(LVGL_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-voice-integration: $(BUILD)/voice_integration_test
	./$(BUILD)/voice_integration_test
.PHONY: test-voice-integration

test-voice-wss:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SRC_DIR)/voice_protocol.cpp $(SRC_DIR)/voice_fragments.cpp $(SRC_DIR)/voice_view_model.cpp voice_wss_replay.cpp -o $(BUILD)/voice_wss_replay
	python3 voice_wss_fixture.py $(BUILD)/voice_wss_replay
.PHONY: test-voice-wss

# Five-minute deterministic malformed-frame stress; local synthetic data only.
test-voice-mutation:
	@mkdir -p $(BUILD)
	clang++ -O1 -g -std=c++17 -fsanitize=address,undefined -I$(SRC_DIR) $(SRC_DIR)/voice_protocol.cpp $(SRC_DIR)/voice_fragments.cpp voice_protocol_fuzz.cpp voice_mutation_main.cpp -o $(BUILD)/voice_mutation
	./$(BUILD)/voice_mutation fixtures/voice_snapshot.json
.PHONY: test-voice-mutation

$(BUILD)/voice_latency_test.o: voice_latency_test.cpp $(SRC_DIR)/voice_push_client.h $(SRC_DIR)/ui_voice.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@
$(BUILD)/voice_latency_test: $(BUILD)/voice_latency_test.o $(BUILD)/voice_push_client_sim.o $(BUILD)/voice_protocol_sim.o $(BUILD)/voice_fragments_sim.o $(BUILD)/voice_view_model_sim.o $(BUILD)/voice_config_sim.o $(BUILD)/ui_voice.o $(BUILD)/ui_dashboard.o $(BUILD)/stubs.o $(BUILD)/photo_frame.o $(BUILD)/photo_frame_runtime.o $(BUILD)/web_settings.o $(BUILD)/ui_strings.o $(BUILD)/weather_icons.o $(FONT_OBJS) $(CJSON_OBJ) $(LVGL_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -lm -lpthread

test-voice-latency: $(BUILD)/voice_latency_test
	./$(BUILD)/voice_latency_test
.PHONY: test-voice-latency

.PHONY: test-voice-reset
test-voice-reset:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) voice_reset_test.cpp $(SRC_DIR)/voice_reset.cpp -o $(BUILD)/voice_reset_test
	./$(BUILD)/voice_reset_test
