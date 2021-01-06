# SwapBank
Memory-based Swap Solution for VMs on a physical machine using compressing and sharing of pages.

This code is a Kernel module that implemented by C and refered [compcache](https://code.google.com/archive/p/compcache/) and [zram](https://en.wikipedia.org/wiki/Zram) of linux.

## Environments
- Hypervisor : Xen 4.0
- Guest OS : para-virtualized Linux-2.6.32 and Linux-2.6.37.1

## Paper
This project is published to the [paper](https://ieeexplore.ieee.org/document/7027336) in 2013.

### Abstraction
Virtualization has recently been applied to consumer electronic (CE) devices such as smart TVs and smartphones. 
In these virtualized CE devices, memory is a valuable resource, because the virtual machines (VMs) on the devices must share the same physical memory. 
However, physical memory is usually partitioned and allocated to each VM. 
This partitioning technique may result in memory shortages, which can seriously degrade application performance.
This paper proposes a new swap mechanism for virtualized CE devices with flash memory. 
This proposed mechanism reduces memory consumption by compressing and sharing unused pages. 
This swap mechanism stores the unused page in memory of another VM, to increase the available memory of the original VM. 
The proposed swap mechanism is implemented on the Xen hypervisor and Linux. 
The mechanism improves the application performance by up to 38% by significantly reducing the number of swap-out requests. 
The swap-out requests are reduced by up to 88% compared to previous swap mechanisms. 
Moreover, the mechanism reduces memory consumption of the swap area by up to 79%.

