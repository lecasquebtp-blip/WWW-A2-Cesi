#!/bin/bash
TOOL="copy"
ENV="nucleo_f401re"
BIN=".pio/build/$ENV/firmware.bin"

cd "$(dirname "$0")" || exit 1
if command -v pio >/dev/null 2>&1; then
    PIO="pio"
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    PIO="$HOME/.platformio/penv/bin/pio"
elif python3 -m platformio --version >/dev/null 2>&1; then
    PIO="python3 -m platformio"
else
    PIO="python -m platformio"
fi

if grep -q '^#define DEBUG 1' src/main.c; then
    echo "Mode debug actif"
    export PLATFORMIO_BUILD_FLAGS="-save-temps=obj"
fi

echo "Compilation..."
if ! $PIO run -e "$ENV"; then
    echo "ECHEC de la compilation"
    exit 1
fi

if [ -n "$PLATFORMIO_BUILD_FLAGS" ]; then
    rm -rf .debug
    mkdir -p .debug/1_preproc .debug/2_compil
    find ".pio/build/$ENV/src" -name '*.i' -exec cp {} .debug/1_preproc/ \;
    find ".pio/build/$ENV/src" -name '*.s' -exec cp {} .debug/2_compil/ \;
    echo "Fichiers intermediaires dans .debug/"
fi

if [ "$1" == "upload" ]; then
    if [ "$TOOL" == "stlink" ]; then
        $PIO run -e "$ENV" -t upload || exit 1
    else
        DRIVE=""
        for d in /media/"$USER"/NODE_F401RE /[d-z]; do
            if [ -f "$d/MBED.HTM" ]; then DRIVE="$d"; break; fi
        done
        if [ -z "$DRIVE" ]; then
            echo "Lecteur introuvable : la carte est branchee ?"
            exit 1
        fi
        cp "$BIN" "$DRIVE/" && sync
        sleep 3
        if [ -f "$DRIVE/FAIL.TXT" ]; then
            echo "ECHEC du televersement :"
            cat "$DRIVE/FAIL.TXT"
            exit 1
        fi
        echo "Firmware copie sur $DRIVE : la carte redemarre."
    fi
fi

if [ "$1" == "monitor" ] || [ "$2" == "monitor" ]; then
    $PIO device monitor -b 115200
fi
