# Add a firewall rule to allow UDP traffic on eRPC's port (31850)
sudo cp erpc.xml /etc/firewalld/services/erpc.xml
sudo firewall-cmd --permanent --add-service=erpc
sudo firewall-cmd --reload

