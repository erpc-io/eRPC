# make
if [ -f /etc/fedora-release ]; then
  sudo cp $(dirname $0)/src/.libs/libmlx4-rdmav2.so /usr/lib64/libmlx4-rdmav2.so
fi

if [ -f /etc/lsb-release ]; then
  sudo cp $(dirname $0)/src/.libs/*-rdmav2.so /usr/lib/libibverbs/
fi

