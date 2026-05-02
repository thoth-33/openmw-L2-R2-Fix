#!/bin/bash -e

docs/source/install_luadocumentor_in_docker.sh
PATH=$PATH:~/luarocks/bin

pushd .
echo "Install Teal Cyan"
luarocks install cyan 0.4.1-1
popd

cyan version
scripts/generate_teal_declarations.sh ./teal_declarations
