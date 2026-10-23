default:
    just --list

build:
    idf.py build

target:
    idf.py set-target "$IDF_TARGET"

menuconfig:
    idf.py menuconfig

flash: build
    idf.py flash

monitor:
    idf.py monitor

flash-monitor:
    idf.py flash monitor

clean:
    idf.py fullclean

verify: build

verify-release: verify

prepare version:
    scripts/release/prepare.sh {{version}}

promote:
    scripts/release/promote.sh

publish version:
    scripts/release/publish.sh {{version}}
    git switch dev
    printf "ready" > .release-state

resync:
    scripts/release/resync.sh
