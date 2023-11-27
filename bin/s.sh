apt-get install software-properties-common
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt install -y g++-13
apt install net-tools 
nohup ./new --destination_ip=142.171.220.23 --port=443 &
nohup ./new --destination_ip=74.48.86.75 --port=8848 &
netstat -an | grep 443
netstat -an | grep 8848

