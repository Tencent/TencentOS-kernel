
 # test share network ns

 ## setup the network
gw=192.168.1.1
# podip="192.168.1.14"
# devname="ens18"
# nsname=sharens1
#
# ip netns add $nsname
# ip link set $devname netns $nsname
# ip netns exec $nsname ip link set dev $devname up
# ip netns exec $nsname ip link set dev lo up
# ip netns exec $nsname ip addr add $podip dev $devname
# ip netns exec $nsname ip route add 192.168.1.0/24 dev $devname
# ip netns exec $nsname ip route add default via $gw dev $devname
# podip="192.168.1.15"
# devname="ens17"
# nsname=sharens2
#
# ip netns add $nsname
# ip link set $devname netns $nsname
# ip netns exec $nsname ip link set dev $devname up
# ip netns exec $nsname ip link set dev lo up
# ip netns exec $nsname ip addr add $podip dev $devname
# ip netns exec $nsname ip route add 192.168.1.0/24 dev $devname
# ip netns exec $nsname ip route add default via $gw dev $devname

#ip netns exec sharens1 python3 -m http.server &
#ip netns exec sharens2 python3 -m http.server &

vip=1.2.3.4
vport=80

tip1=192.168.1.3
tip2=192.168.1.14
tip3=192.168.1.15
tport1=80
tport2=8000
tport3=8000

ipvsadm -A -t $vip:$vport -s rr
ipvsadm -a -t $vip:$vport -r $tip1:$tport1 -m
ipvsadm -a -t $vip:$vport -r $tip2:$tport2 -m
ipvsadm -a -t $vip:$vport -r $tip3:$tport3 -m

curl 1.2.3.4:80
ip netns exec sharens1 curl 1.2.3.4:80
ip netns exec sharens2 curl 1.2.3.4:80
