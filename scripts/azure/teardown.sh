az vm delete --resource-group myResourceGroup --name myVM --yes
az network nic delete --resource-group myResourceGroup --name myNic2
az network nic delete --resource-group myResourceGroup --name myNic1
az network public-ip delete --resource-group myResourceGroup --name myPublicIp
az network nsg rule delete --resource-group myResourceGroup --nsg-name myNetworkSecurityGroup --name Allow-SSH-Internet
az network nsg delete --resource-group myResourceGroup --name myNetworkSecurityGroup
az network vnet subnet delete  --resource-group myResourceGroup --vnet-name myVnet --name mySubnetBackEnd
az network vnet delete  --resource-group myResourceGroup --name myVnet


