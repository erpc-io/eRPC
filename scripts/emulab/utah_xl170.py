"""eRPC experiment.

Instructions:
NA
"""

import geni.portal as portal
import geni.rspec.pg as rspec

pc = portal.Context()
pc.defineParameter("num_nodes", "Number of nodes", portal.ParameterType.INTEGER, 10, [])
        
request = pc.makeRequestRSpec()
lan = request.LAN("lan")
lan.bandwidth = 25000
# LAN link multiplexing must be off

params = pc.bindParameters()

for i in range(params.num_nodes):
    node = request.RawPC("akalianode-" + str(i + 1))
    node.disk_image = 'urn:publicid:IDN+utah.cloudlab.us+image+ron-PG0:akalia-RDMA'
    #node.disk_image = 'urn:publicid:IDN+emulab.net+image+emulab-ops:UBUNTU16-64-STD'
    node.hardware_type = "xl170"
    iface = node.addInterface("if1")
    lan.addInterface(iface)
    node.addService(rspec.Execute(shell="bash", command="/proj/ron-PG0/akalia/setup-cloudlab.sh"))

pc.printRequestRSpec(request)
