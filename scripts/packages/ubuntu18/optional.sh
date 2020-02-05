#!/bin/bash
#
# Convenience packages for eRPC development, and application-specific packages

# General packages not specific to eRPC
sudo apt install -y numactl bc htop memcached libmemcached-dev clang-format \
  exuberant-ctags silversearcher-ag sloccount apcalc

# libpmem currently requires a ppa
sudo add-apt-repository -y ppa:ahasenack/nvdimm-update
sudo apt -y install libpmem-dev

# Fuzzy find configuration
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
yes | ~/.fzf/install

# Systemish
git clone https://github.com/anujkaliaiitd/systemish.git ~/systemish
~/systemish/configs/copy.sh

# Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim
vim -c 'PluginInstall' -c 'qa!'

# Ripgrep
sudo add-apt-repository ppa:x4121/ripgrep
sudo apt-get update
sudo apt install ripgrep
