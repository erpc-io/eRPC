#!/bin/bash
#
# This script installs pre-requisite packages for installing Mellanox OFED
# on a Fedora box.
# 
# After running this script, run sudo ./mlnxofedinstall --add-kernel-support

sudo dnf -y install createrepo python2-devel elfutils-libelf-devel \
  redhat-rpm-config rpm-build python2-libxml2 gtk2 gcc-gfortran atk tk tcl tcsh \
  perl
