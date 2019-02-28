@nasm -f obj -o bin/loader.obj src/loader.asm
@jloc loader.lnk bin/loader.com map/loader.map
