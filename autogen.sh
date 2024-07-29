#!/bin/bash

# Determine the source directory
srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

# Save the current directory
olddir=`pwd`

# Change to the source directory
cd "$srcdir"

# Run autoreconf
autoreconf --verbose --force --install || {
  echo 'autogen.sh failed'
  exit 1
}

# Return to the original directory
cd "$olddir"

echo
echo "Now you can proceed to configure this project"
echo "In the build directory, run:"
echo "  ./configure --prefix /usr/ --libdir /usr/lib/x86_64-linux-gnu/"
echo "and then run:"
echo "  make"
echo "  sudo make install"
echo
