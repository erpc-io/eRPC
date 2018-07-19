#!/bin/bash
#
# This script sets up a fresh Ubuntu 18.04 box for eRPC.
#
# For best perforamance, Mellanox OFED should be installed by downloading from
# Mellanox. However, eRPC should work with upstream mlx* packages as well

###
### Required packages
###

sudo apt update
sudo apt -y install g++-8 cmake libnuma-dev numactl bc libgflags-dev

# GTest is special for some reason
sudo apt -y install libgtest-dev
(cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libg* /usr/lib/)

###
### Optional convenience packages
###

# General packages not specific to eRPC
sudo apt install -y htop memcached libmemcached-dev clang-format \
  exuberant-ctags silversearcher-ag sloccount apcalc

# Fuzzy find configuration
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
yes | ~/.fzf/install

# Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim
