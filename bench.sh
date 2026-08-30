#!/bin/zsh

shopt -s expand_aliases;

if [[ $# -ne 1 ]]; then
    printf "\x1b[1;33mUsage:\x1b[0m ./bench.sh [<bench name>]\n";
    exit 1;
fi

testname="$1";
warmups=5;
runs=20;

hyperfine -w $warmups -r $runs -N --command-name=qjsbench "qjs ./microbench/$testname.js" --command-name=tjbench "./build/toyjit";
