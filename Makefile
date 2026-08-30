.PHONY: all build test clean

all: build

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

clean:
	cmake -E remove_directory build
