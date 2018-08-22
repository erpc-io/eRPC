# Create a virtual network with a frontend subnet
az network vnet create \
    --resource-group myResourceGroup \
    --name myVnet \
    --address-prefix 10.0.0.0/16 \
    --subnet-name mySubnetFrontEnd \
    --subnet-prefix 10.0.1.0/24

# Create a backend subnet in the virtual network
az network vnet subnet create \
    --resource-group myResourceGroup \
    --vnet-name myVnet \
    --name mySubnetBackEnd \
    --address-prefix 10.0.2.0/24

# A network security group
az network nsg create \
    --resource-group myResourceGroup \
    --name myNetworkSecurityGroup

# Allow SSH access
az network nsg rule create \
  --resource-group myResourceGroup \
  --nsg-name myNetworkSecurityGroup \
  --name Allow-SSH-Internet \
  --access Allow \
  --protocol Tcp \
  --direction Inbound \
  --priority 100 \
  --source-address-prefix Internet \
  --source-port-range "*" \
  --destination-address-prefix "*" \
  --destination-port-range 22

# A public IP for the frontend
az network public-ip create \
    --name myPublicIp \
    --resource-group myResourceGroup

# A non-accelerated NIC with a public IP for the front end
az network nic create \
    --resource-group myResourceGroup \
    --name myNic1 \
    --vnet-name myVnet \
    --subnet mySubnetFrontEnd \
    --public-ip-address myPublicIp \
    --network-security-group myNetworkSecurityGroup

# An accelerated NIC without a public IP for the back end
az network nic create \
    --resource-group myResourceGroup \
    --name myNic2 \
    --vnet-name myVnet \
    --subnet mySubnetBackEnd \
    --accelerated-networking true \
    --network-security-group myNetworkSecurityGroup

# Create the VM with two NICs
az vm create \
    --resource-group myResourceGroup \
    --name myVM \
    --image Canonical:UbuntuServer:18.04-LTS:18.04.201804262 \
    --size Standard_D4S_v3 \
    --admin-username akalia \
    --generate-ssh-keys \
    --nics myNic1 myNic2
