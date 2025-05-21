#!/bin/bash
# Add the new submodule linkage
git submodule add https://github.com/OpenCPN/opencpn-libs.git
git commit -m "Adding revised opencpn-libs submodule main"
# Added new linkage to module opencpn-libs main and made commit.
git log --oneline

