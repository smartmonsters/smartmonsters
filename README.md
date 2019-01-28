Smart Monsters beta test
========================

Smart Monsters on the blockchain: crypto currency and decentralized game with strategy and RPG elements.

This beta test always starts as testnet, using 'smartmonsters/testnet-beta' data folder.

Newest Windows client:

https://smartmonsters.github.io/Manual4.html

To build on a new Ubuntu 16.04 or Linux Mint 18

    sudo apt-get install libboost-chrono-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev
    sudo apt-get install libboost-dev git qt4-qmake libqt4-dev build-essential qt4-linguist-tools libssl-dev
    sudo apt-get install libdb++-dev

In case of 'cannot find -lgthread-2.0'

    sudo apt-get install libgtk2.0-dev

Additionally, to build on Ubuntu 18.04 or Linux Mint 19

    sudo apt-get install libssl1.0.0 libssl1.0-dev

if Qt Creator is installed after these dependencies, open smartmonsters-qt.pro, and click Build | Build project smartmonsters-qt, otherwise

    qmake
    make

To build the daemon (without UPNP support)

    cd src
    make -f Makefile USE_UPNP=

