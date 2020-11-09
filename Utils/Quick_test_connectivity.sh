#!/bin/bash

# Last update: 09/11/2020 - 18:18

# User-configurable parameters
IP_DATA_REMOTE=172.22.47.249 # IP of the slave board (test interface)
IP_CONTROL_REMOTE=10.0.2.109 # IP of the slave board (control interface)
PORT_BIDIR=46001			 # port_late_bidir in LTNT.ini
PORT_UNIDIR_UL=46002		 # port_late_unidir_UL in LTNT.ini
PORT_UNIDIR_DL=46003         # port_late_unidir_DL in LTNT.ini
INTERFACE="eth0"             # test_interface in LTNT.ini (defaul in LTNT Test Manager: eth0)

MY_IP_DATA=172.22.47.250	 		# my_ip_data in LTNT.ini
MY_IP_CONTROL_REMOTE=10.0.2.108		# my_ip_control in LTNT.ini

UDP_IPERF_PKT_LEN=1400              # UDP_iperf_packet_len in LTNT.ini
TCP_IPERF_BUF_LEN=128K              # TCP_iperf_buf_len in LTNT.ini

SSH_PORT=22					  		# SSH port to connect to the slave board via SSH, on the control interface


## LaTe tests
# Test RTT connectivity
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "nohup /root/LaTe -s -u -p ${PORT_BIDIR} -S ${INTERFACE} >/dev/null 2>&1 &"
/root/LaTe -c ${IP_DATA_REMOTE} -u -B -t 100 -n 5 -p ${PORT_BIDIR} -S ${INTERFACE} >/dev/null 2>&1

if [ $? -ne 0 ]; then
	echo "LaTe: RTT connectivity test on port ${PORT_BIDIR} failed."
	exit 1
else
	echo "LaTe: RTT connectivity test on port ${PORT_BIDIR} succeeded."
fi

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 LaTe >/dev/null 2>&1"
killall -9 LaTe >/dev/null 2>&1


# Test UL connectivity
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "nohup /root/LaTe -s -u -p ${PORT_UNIDIR_UL} -S ${INTERFACE} >/dev/null 2>&1 &"
/root/LaTe -c ${IP_DATA_REMOTE} -u -U -t 100 -n 5 -p ${PORT_UNIDIR_UL} -S ${INTERFACE} >/dev/null 2>&1
if [ $? -ne 0 ]; then
	echo "LaTe: UL connectivity test on port ${PORT_UNIDIR_UL} failed."
	exit 1
else
	echo "LaTe: UL connectivity test on port ${PORT_UNIDIR_UL} succeeded."
fi

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 LaTe >/dev/null 2>&1"
killall -9 LaTe >/dev/null 2>&1


# Test DL connectivity
/root/LaTe -s -u -p ${PORT_UNIDIR_DL} -S ${INTERFACE} >/dev/null 2>&1 &
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "/root/LaTe -c ${MY_IP_DATA} -u -U -t 100 -n 5 -p ${PORT_UNIDIR_DL} -S ${INTERFACE} >/dev/null 2>&1"

if [ $? -ne 0 ]; then
	echo "LaTe: DL connectivity test on port ${PORT_UNIDIR_DL} failed."
	exit 1
else
	echo "LaTe: DL connectivity test on port ${PORT_UNIDIR_DL} succeeded."
fi

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 LaTe >/dev/null 2>&1"
killall -9 LaTe >/dev/null 2>&1




## iperf tests
# Test DL connectivity (UDP)
iperf -s -u -i 1 -t 3 -p 9000 -l ${UDP_IPERF_PKT_LEN} > tmpiperfUDP.tmplog &
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "iperf -c ${MY_IP_DATA} -p 9000 -i 1 -t 3 -u -b 1M -l ${UDP_IPERF_PKT_LEN} >/dev/null 2>&1"

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 iperf >/dev/null 2>&1"
killall -9 iperf >/dev/null 2>&1

cat tmpiperfUDP.tmplog | grep "read failed" >/dev/null
if [ $? -eq 0 ]; then
	echo "iperf: DL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) reported a read failed error. Protocol: UDP."
	echo "[ERROR] The test may succeed, but there is no reliable connection."
fi

cat tmpiperfUDP.tmplog | grep "connected with" >/dev/null

if [ $? -ne 0 ]; then
	echo "iperf: DL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) failed. Protocol: UDP."
	rm tmpiperfUDP.tmplog
	exit 1
else
	echo "iperf: DL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) succeeded. Protocol: UDP."
	rm tmpiperfUDP.tmplog
fi


# Test UL connectivity (UDP)
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "nohup iperf -s -u -i 1 -t 3 -p 9000 -l ${UDP_IPERF_PKT_LEN} > tmpiperfUDP.tmplog &"
iperf -c ${IP_DATA_REMOTE} -p 9000 -i 1 -t 3 -u -b 1M -l ${UDP_IPERF_PKT_LEN} >/dev/null 2>&1

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 iperf >/dev/null 2>&1"
killall -9 iperf >/dev/null 2>&1

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "cat tmpiperfUDP.tmplog | grep \"read failed\" >/dev/null"
if [ $? -eq 0 ]; then
	echo "iperf: UL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) reported a read failed error. Protocol: UDP."
	echo "[ERROR] The test may succeed, but there is no reliable connection."
fi

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "cat tmpiperfUDP.tmplog | grep \"connected with\" >/dev/null"

if [ $? -ne 0 ]; then
	echo "iperf: UL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) failed. Protocol: UDP."
	ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "rm tmpiperfUDP.tmplog"
	exit 1
else
	echo "iperf: UL connectivity test on port 9000 (packet size: ${UDP_IPERF_PKT_LEN}) succeeded. Protocol: UDP."
	ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "rm tmpiperfUDP.tmplog"
fi


# Test DL connectivity (TCP)
iperf -s -i 1 -t 3 -p 9000 -l ${TCP_IPERF_BUF_LEN} > tmpiperfTCP.tmplog &
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "iperf -c ${MY_IP_DATA} -p 9000 -i 1 -t 3 -b 1M -l ${TCP_IPERF_BUF_LEN} >/dev/null 2>&1"

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 iperf >/dev/null 2>&1"
killall -9 iperf >/dev/null 2>&1

cat tmpiperfTCP.tmplog | grep "read failed" >/dev/null
if [ $? -eq 0 ]; then
	echo "iperf: DL connectivity test on port 9000 reported a read failed error. Protocol: TCP."
	echo "[ERROR] The test may succeed, but there is no reliable connection."
fi

cat tmpiperfTCP.tmplog | grep "connected with" >/dev/null

if [ $? -ne 0 ]; then
	echo "iperf: DL connectivity test on port 9000 failed. Protocol: TCP."
	rm tmpiperfTCP.tmplog
	exit 1
else
	echo "iperf: DL connectivity test on port 9000 succeeded. Protocol: TCP."
	rm tmpiperfTCP.tmplog
fi


# Test UL connectivity (TCP)
ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "nohup iperf -s -i 1 -t 3 -p 9000 -l ${TCP_IPERF_BUF_LEN} > tmpiperfTCP.tmplog &"
iperf -c ${IP_DATA_REMOTE} -p 9000 -i 1 -t 3 -b 1M -l ${TCP_IPERF_BUF_LEN} >/dev/null 2>&1

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "killall -9 iperf >/dev/null 2>&1"
killall -9 iperf >/dev/null 2>&1

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "cat tmpiperfTCP.tmplog | grep \"read failed\" >/dev/null"
if [ $? -eq 0 ]; then
	echo "iperf: UL connectivity test on port 9000 reported a read failed error. Protocol: TCP."
	echo "[ERROR] The test may succeed, but there is no reliable connection."
fi

ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "cat tmpiperfTCP.tmplog | grep \"connected with\" >/dev/null"

if [ $? -ne 0 ]; then
	echo "iperf: UL connectivity test on port 9000 failed. Protocol: TCP."
	ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "rm tmpiperfTCP.tmplog"
	exit 1
else
	echo "iperf: UL connectivity test on port 9000 succeeded. Protocol: TCP."
	ssh -o StrictHostKeyChecking=no -p ${SSH_PORT} root@${IP_CONTROL_REMOTE} "rm tmpiperfTCP.tmplog"
fi

echo "Everything is working fine. All the tests were completed successfully."
