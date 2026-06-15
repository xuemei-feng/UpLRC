pkill -9 run_datanode
pkill -9 run_proxy

./project/cmake/build/run_datanode 10.10.1.4:17600 & 
./project/cmake/build/run_datanode 10.10.1.5:17601 & 
./project/cmake/build/run_datanode 10.10.1.6:17602 & 
./project/cmake/build/run_datanode 10.10.1.7:17603 & 
./project/cmake/build/run_datanode 10.10.1.8:17604 & 
./project/cmake/build/run_datanode 10.10.1.9:17605 & 
./project/cmake/build/run_datanode 10.10.1.10:17606 & 
./project/cmake/build/run_datanode 10.10.1.11:17607 & 

sleep 5s

./project/cmake/build/run_proxy 10.10.1.3:50405  & 

