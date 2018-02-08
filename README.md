Smart Monsters alpha test
=========================

Smart Monsters on the blockchain: crypto currency and decentralized game

Smart Monsters is a complete overhaul of Huntercoin as a simulation game with strategy and RPG elements.

It doesn't interfere with an existing installation of Huntercoin, always starts on testnet, using 'smartmonsters/testnet-alpha' data folder

Newest Windows build:

https://smartmonsters.github.io/Manual4.html

To build on a new Ubuntu 16.04 or Linux Mint 18

    sudo apt-get install libboost-chrono-dev libboost-date-time-dev libboost-filesystem-dev libboost-program-options-dev libboost-serialization-dev libboost-system-dev libboost-thread-dev
    sudo apt-get install libboost-dev git qt4-qmake libqt4-dev build-essential qt4-linguist-tools libssl-dev
    sudo apt-get install libdb++-dev

if Qt Creator is installed after this, open smartmonsters-qt.pro, and Build | Build project smartmonsters-qt, otherwise

    qmake
    make

To build the daemon (without UPNP support)

    cd src
    make -f Makefile USE_UPNP=

In case of 'cannot find -lgthread-2.0'

    apt-get install libgtk2.0-dev

