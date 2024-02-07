<br>
<p align="center">
  <img width=384 src="https://download.nap-labs.tech/identity/svg/logos/nap_logo_blue.svg">
</p>
	
# Description

[Ethercat](https://www.ethercat.org/default.htm) master interface for NAP using [SOEM](https://github.com/OpenEtherCATsociety/SOEM.git)

## Installation
Compatible with NAP 0.6 and higher - [package release](https://github.com/napframework/nap/releases) and [source](https://github.com/napframework/nap) context. 

### From ZIP

[Download](https://github.com/naivisoftware/napsoem/archive/refs/heads/main.zip) the module as .zip archive and install it into the nap `modules` directory:
```
cd tools
./install_module.sh ~/Downloads/napsoem-main.zip
```

### From Repository

Clone the repository and setup the module in the nap `modules` directory.

```
cd modules
clone https://github.com/naivisoftware/napsoem.git
./../tools/setup_module.sh napsoem
```
