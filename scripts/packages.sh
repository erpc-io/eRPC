#!/bin/bash
#
# This script sets up a fresh Ubuntu box for eRPC.
#
# For best perforamance, Mellanox OFED should be installed by downloading from
# Mellanox. However, eRPC should work with upstream mlx* packages as well

###
### Required packages
###

sudo apt-get -y install cmake libnuma-dev libpapi-dev numactl libgflags-dev libboost-date-time-dev libenet-dev

# GTest is special for some reason
sudo apt-get -y install libgtest-dev
(cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libg* /usr/lib/)

###
### Optional convenience packages
###

# g++-7
sudo apt-get update && \
  sudo apt-get install build-essential software-properties-common -y && \
  sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
  sudo apt-get update && \
  sudo apt-get install g++-7

# General packages not specific to eRPC
sudo apt-get install -y htop memcached libmemcached-dev clang-format exuberant-ctags

# Fuzzy find
sudo apt-get install -y silversearcher-ag
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
~/.fzf/install

# Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim
