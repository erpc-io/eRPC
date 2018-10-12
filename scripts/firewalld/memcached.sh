# Add a firewall rule to allow UDP traffic on memcached's ports
sudo cp memcached.xml /etc/firewalld/services/memcached.xml
sudo firewall-cmd --permanent --add-service=memcached
sudo firewall-cmd --reload

