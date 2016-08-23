This folder has the scripts and guidelines for running the same evaluation of Trumpet as presented in the paper. 
Here, we describe how to setup dpdk and the code.
- Download DPDK http://dpdk.org/download. This guide is tested with version 2.2
- Open the archive, say in your home folder
- You need hugepages. Install hugepages package. In ubuntu use "sudo apt-get install hugepages"
- You also need compiling libraries. Just install build-essential package
- Now you can use the script provided in <DPDKFolder>tools/setup.sh file to set it up
- Compile: On linux and 64-bit machines, type number 14 for "x86_64-native-linuxapp-gcc"
- Install NIC driver module: type number 17 for "Insert IGB UIO module"
- Install KNI module: type number 19 for "Insert KNI module"
- hugepages: type number 21 for "Setup hugepage mappings for NUMA systems"
- Add NICs to IGB driver:
 - Type 23 "Bind Ethernet device to IGB UIO module" and you will get the list of NICs in the machine
 - Each line represents a port. A line may be like "0000:82:00.1 '82599 10 Gigabit TN Network Connection' if=eth7 drv=ixgbe unused=igb_uio"
 - type the PCI address. For the above example it is 0000:82:00.1
- Now add the following lines at the end of .bashrc file in your home folder. Replace <PATH TO DPDK> with the path of where you put the dpdk folder
 - export RTE_SDK=<PATH TO DPDK>
 - export RTE_TARGET=x86_64-native-linuxapp-gcc
 - To make sure bash reads the config again you may make a new terminal or just run bash command.
- Now we need to isolate CPU cores for Trumpet from other processes. We can do that by adding the following line into /etc/default/grub.
 - This command isolates cores with odd number (cores on CPU 1 on a NUMA architecture). Add it to the file. GRUB_CMDLINE_LINUX="iommu=pt intel_iommu=on isolcpus=1,3,5,7,9,11,13,15,17,19" 
 - If you are using a NUMA architecture, you first need to find out to which CPU the NIC is connected. Processing the packets on the CPU that is directly connected to the NIC is faster. For this run "cat /sys/class/net/eth6/device/numa_node" assuming the NIC is eth6. If it is CPU 0, you also need to update the core mask in the dpdk parameter list passed inside run.sh.
 - run sudo grub-update, and reboot.
- Also make sure that you have sudo access
- Make sure you can get sudo access without the need to type password. For this you need to add the following line in the sudoers file. run "sudo visudo". Then add the "masoud ALL=(ALL:ALL) NOPASSWD: ALL". Replace masoud with your username. Push ctrl+x and choose yes to save and exit.
- You either need to pass password to the ssh command or setup public/private key authentication for that. To use public/private key use the steps here: https://help.ubuntu.com/community/SSH/OpenSSH/Keys. If you choose a passphrase for the key pair, you may want to use the ssh-agent to remember it. Look at http://rabexc.org/posts/using-ssh-agent. So after this you should be able to ssh from the receiver machine to the sender without using the password.
- There are three components in this package provided in separate folders: receiver, sender and controller. Make sure you can compile all three. Each component has a -h commandline that shows the commandline parameter help.

Done.
You can reverse the config by running the tools/setup.sh again and
- Unbind nics: 29 for "Unbind NICs from IGB UIO or VFIO driver". You may be asked to type the  name of the driver to put the port back to it. For the above exxample it is ixgbe
- Unload IGB and KNI using 30 and 32
- Remove huge pages using number 33


You may start with the experiments/base10.
