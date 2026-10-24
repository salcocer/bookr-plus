#!/bin/sh
# Builds Bookr Plus: the bundled MuPDF and DjVu libraries first, then the app.
# Needs the pspdev toolchain on the PATH (https://github.com/pspdev/pspdev).
set -e
cd "$(dirname "$0")/Bookr"
make -C mupdf
make -C djvu
make
echo "Built Bookr/EBOOT.PBP"
