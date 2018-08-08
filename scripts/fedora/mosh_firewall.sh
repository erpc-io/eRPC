# Add a firewall rule to allow UDP traffic on mosh's ports
sudo cp mosh.xml /etc/firewalld/services/mosh.xml
sudo firewall-cmd --permanent --add-service=mosh
sudo firewall-cmd --reload

