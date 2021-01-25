#!/usr/bin/env sh

print_usage() {
  echo "Usage: $(basename $0) [OPTIONS]"
  echo
  echo "Options:"
  echo "        -h|--help               print this help"
  echo "        -d|--debug              setup debug build"
  echo "        -b|--build PATH         build path (defult 'build')"
  echo
}

OPTIONS=""
BUILD="build"

while [ ! -z $1 ]; do
  case "$1" in
    '-h' | '--help')
      print_usage
      exit 1
    ;;
    '-d' | '--debug')
      OPTIONS="$OPTIONS --buildtype=debug -Db_sanitize=address,undefined"
      shift
    ;;
    '-b' | '--build')
      BUILD="$2"     
      shift 2
    ;;
    *)
      echo "Unknown option '$1'" >&2
      exit 1
    ;;
  esac
done

  
#meson setup --buildtype=debug build -Db_sanitize=address,undefined
meson setup $OPTIONS $BUILD
