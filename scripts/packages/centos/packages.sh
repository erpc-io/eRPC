#!/bin/bash
#
# This script sets up a fresh CentOS-7 box for eRPC.
#
# For best perforamance, Mellanox OFED should be installed by downloading from
# Mellanox. However, eRPC should work with upstream mlx* packages as well

# Update to the latest CentOS (e.g., CentOS 7.4 -> CentOS 7.5)
# sudo yum -y update

# Install EPEL (Extra Packages for Enterprise Linux), needed for many packages below
sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm

###
### Packages required for eRPC
###
sudo yum -y install git gcc-c++ cmake numactl-devel numactl bc gflags-devel \
  libpmem libpmem-devel dpdk dpdk-devel

###
### Packages required to install Mellanox OFED
###
sudo yum -y install createrepo python2-devel elfutils-libelf-devel \
  redhat-rpm-config rpm-build libxml2-python gtk2 \
  gcc-gfortran atk tk tcl tcsh perl

###
### Optional convenience packages
###

# Update vim. Find a repo from Fedora Copr if this link doesn't work
sudo curl -L https://copr.fedorainfracloud.org/coprs/hnakamur/vim/repo/epel-7/hnakamur-vim-epel-7.repo -o /etc/yum.repos.d/hnakamur-vim-epel7.repo
sudo yum -y update vim*

# General packages not specific to eRPC
sudo yum -y install htop memcached libmemcached-devel ctags-etags \
  the_silver_searcher sloccount calc

# Fuzzy find configuration
git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf
yes | ~/.fzf/install

# Vundle
git clone https://github.com/VundleVim/Vundle.vim.git ~/.vim/bundle/Vundle.vim
