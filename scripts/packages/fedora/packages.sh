#!/bin/bash
#
# This script sets up a fresh Fedora 27 box for eRPC.
#
# For best perforamance, Mellanox OFED should be installed by downloading from
# Mellanox. However, eRPC should work with upstream mlx* packages as well

###
### Required packages
###

sudo dnf -y install gcc-c++ cmake numactl-devel numactl bc gflags-devel \
  libpmem libpmem-devel dpdk-devel dpdk

###
### Optional convenience packages
###

# Fix vim
sudo dnf -y upgrade vim-minimal
sudo dnf -y install vim-enhanced

# General packages not specific to eRPC
sudo dnf -y install ripgrep htop memcached libmemcached-devel git-clang-format \
  ctags-etags the_silver_searcher sloccount calc

# Fuzzy find configuration
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
yes | ~/.fzf/install

# Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim
