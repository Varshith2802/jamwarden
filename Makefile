# JamWarden convenience targets (Linux/macOS; on Windows run the commands inside each target manually)
PY ?= python3

.PHONY: all build test scenarios eval plot demo-dashboard firmware-check firmware clean

all: build test

build:
	cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
	cmake --build core/build -j

test: build
	./core/build/jamwarden_tests
	$(PY) -m pytest -q tests

scenarios:
	$(PY) tools/scenarios.py generate-all --out data/scenarios

eval: build
	$(PY) eval/run_eval.py --seeds 20

plot: build scenarios
	$(PY) tools/plot_session.py data/scenarios/jam_then_spoof.ubx --out docs/session_jam_then_spoof.png

demo-dashboard: build scenarios
	./core/build/jamwarden_replay data/scenarios/jam_then_spoof.ubx > /tmp/jamwarden_demo.jsonl
	$(PY) dashboard/collector.py --file /tmp/jamwarden_demo.jsonl --db jamwarden.db --no-server
	$(PY) dashboard/collector.py --db jamwarden.db

# Compile the ESP32 firmware against Arduino stubs on the PC (catches C++ errors without a board)
firmware-check:
	g++ -std=c++17 -Wall -Wextra -DJW_WIFI=1 -Ifirmware/esp32/test/host_stub -Ifirmware/esp32/include -Icore/include \
	    firmware/esp32/src/main.cpp firmware/esp32/test/host_stub/stub_main.cpp core/src/*.cpp -o /tmp/jw_fw_host
	/tmp/jw_fw_host

# Real build for the board (needs PlatformIO: pip install platformio)
firmware:
	cd firmware/esp32 && pio run

clean:
	rm -rf core/build jamwarden.db
