
# 6G Multi-Access Network Simulator (ns3)
- The source files for the NetworkGym module are located at `contrib/networkgym/` folder.
- The source files for the GMA module (for multi-access and RAN slicing) are located at `contrib/gma/` folder.
- The example scripts are located at `scratch/` folder.

1. Install ns-3.40:
  ```
  git clone -b ns-3.40 https://gitlab.com/nsnam/ns-3-dev.git network_gym_sim
  ```
After downloading ns-3, install the dependencies and libraries following the [ns-3 prerequisites](https://www.nsnam.org/docs/tutorial/html/getting-started.html#prerequisites). Build the ns-3 with the following commands. You can find more information on building ns-3 [here](https://www.nsnam.org/docs/tutorial/html/getting-started.html#building-ns-3).
  ```
  cd network_gym_sim
  ./ns3 clean
  ./ns3 configure --build-profile=optimized --disable-examples --disable-tests
  ./ns3 build
  ```

2. Copy gma and networkgym module files:
  ```
  cp ../netgymsim/scratch/unified-network-slicing.cc scratch/
  cp -r ../netgymsim/contrib/* contrib/
  ```

3. Install the ZeroMQ socket C++ library (required by networkgym module):
  ```
  apt-get install libczmq-dev
  ```

4. Install the 3rd party 5G nr module:
  ```
  cd contrib
  git clone -b 5g-lena-v2.6.y https://gitlab.com/cttc-lena/nr
  ```

5. Install the C++ Json library. Replace the `network_gym_sim/contrib/networkgym/model/json.hpp` with the [json.hpp](https://github.com/nlohmann/json/blob/develop/single_include/nlohmann/json.hpp):
  ```
  cd networkgym/model/
  rm json.hpp
  wget https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp
  ```

6. Finally, we need to fix a few bugs in the ns-3. The lte module hard coded the IP addresses for the backhaul links. This two files allows we to customize the IP addresses for the backhaul links. 
  ```
  cd ../../../../
  cp netgymsim/contrib/modified/no-backhaul-epc-helper.cc network_gym_sim/src/lte/helper/no-backhaul-epc-helper.cc
  cp netgymsim/contrib/modified/point-to-point-epc-helper.cc network_gym_sim/src/lte/helper/point-to-point-epc-helper.cc
  ```

7 Try to build ns-3 once again to see if there is any errors:
  ```
  cd network_gym_sim
  ./ns3 clean
  ./ns3 configure --build-profile=optimized --disable-examples --disable-tests
  ./ns3 build
  ```
8. (Optional) With the previous steps, the code should be running without any issue. However, we also identified a few more issues related to TCP or BBR and proposed fixes in the modified files located in `netgymsim/contrib/modified/` folder. You can also replace the original files with them if needed. Again, this is not required.
