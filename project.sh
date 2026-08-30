#!/bin/zsh

argc=$#;
opt="$1";

help_with_exit() {
    printf "Usage: ./project.sh [help | rebuild <preset> <build tool> | build <preset> <build tool>]\n"
    exit $1;
}

if [[ "$opt" = "help" ]]; then
    help_with_exit 0;
elif [[ "$opt" = "rebuild" ]]; then
    cmake --fresh -S . -B build --preset="$2" -G "$3" && cmake --build build;
elif [[ "$opt" = "build" ]]; then
    cmake -S . -B build --preset="$2" -G "$3" && cmake --build build;
else
    help_with_exit 1;
fi
