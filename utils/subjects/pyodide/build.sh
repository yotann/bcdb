#!/bin/sh
docker build -t pyodide-bitcode .
docker create --name pyodide-bitcode pyodide-bitcode
docker cp pyodide-bitcode:/src/pyodide/pyodide.bc ./
docker rm pyodide-bitcode
