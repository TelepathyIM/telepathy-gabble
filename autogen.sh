#!/bin/sh
set -e

autoreconf -i -f

#Check if submodules should be enabled
enable_submodules=true
for arg in $*; do
    case $arg in
        --disable-submodules)
            enable_submodules=false
            ;;
        *)
            ;;
    esac
done

if test $enable_submodules = true; then
    # Fetch submodules if needed
    if test ! -f lib/ext/wocky/autogen.sh; then
        echo "+ Setting up submodules"
        git submodule init
    fi
    git submodule update

    # launch Wocky's autogen.sh
    cd lib/ext/wocky
    sh autogen.sh --no-configure
    cd ../../..
fi

# Honor NOCONFIGURE for compatibility with gnome-autogen.sh
if test x"$NOCONFIGURE" = x; then
    run_configure=true
    for arg in $*; do
        case $arg in
            --no-configure)
                run_configure=false
                ;;
            *)
                ;;
        esac
    done
else
    run_configure=false
fi

if test $run_configure = true; then
    ./configure "$@"
fi
