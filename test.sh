#!/bin/sh
set -e
docker build --tag pg_json_decoding_test .
docker run --rm -v "$PWD":/src:ro pg_json_decoding_test
