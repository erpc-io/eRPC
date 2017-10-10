#!/bin/bash

# Install g++-7
sudo apt-get update && \
  sudo apt-get install build-essential software-properties-common -y && \
  sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
  sudo apt-get update && \
  sudo apt-get install g++-7

# Install general packages (not specific to eRPC)
sudo apt-get install -y cmake htop libnuma-dev libpapi-dev memcached libmemcached-dev numactl clang-format

# Install fuzzy find
sudo apt-get install -y silversearcher-ag
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
~/.fzf/install

# Install Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim

# Install packages required for eRPC
sudo apt-get -y install cmake libgflags-dev libboost-date-time-dev libenet-dev libgtest-dev

# Really install gtest
(cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libg* /usr/lib/)
