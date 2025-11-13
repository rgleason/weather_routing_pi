REM Update the current opencpn-libs submodule linkage and add new linkage
git submodule update --remote --merge opencpn-libs
git add opencpn-libs
git commit -m "Update opencpn-libs submodule"
git push origin master
REM opencpn-libs linkage and files updated and  commit made