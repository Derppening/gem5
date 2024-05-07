#!/usr/bin/env bash

if ! command -v bear &> /dev/null; then
  echo "Bear <https://github.com/rizsotto/Bear> is required for generating compile_commands.json"
  exit 1
fi

declare -a scon_args

while [ $# -ge 1 ]; do
  case "$1" in
    --help)
      echo "Usage: $0 [--help] [--overwrite] -- [SCON_ARGS...]"
      echo
      echo -e "\t--overwrite: Overwrites the generated compile_commands.json"
      exit
      ;;
    --overwrite)
      overwrite=1
      ;;
    --)
      shift
      break
      ;;
    *)
      break
      ;;
  esac
  shift
done

while [ $# -ge 1 ]; do
  scon_args+=("$1")
  shift
done

if [ -z "$overwrite" ]; then
  bear --append -- scons "${scon_args[@]}"
else
  bear -- scons "${scon_args[@]}"
fi
