## Installing Prerequisites
Currently, SACAI installation process is limited to Linux (due to complications with Windows' Command Prompt). However, the process has been heavily optimized for Linux and is able to even use programs such as **WSL** (Windows Subsystem for Linux) since it doesn't require any /root files. 

### Ubuntu
#### Ubuntu 18.04
Ubuntu is likely the most favorable Linux distribution to install the engine due to its wideband support for backend configurations (such as OpenBSL). To install SACAI on Ubuntu, download the latest release from Github to a directory of your choosing. For Ubuntu 18.04, you will require the latest versions of meson, libstdc++-8-dev, and clang-6.0 before proceeding to the source building:
```
sudo apt-get install libstdc++-8-dev clang-6.0 ninja-build pkg-config
pip3 install meson --user
CC=clang-6.0 CXX=clang++-6.0 INSTALL_PREFIX=~/.local ./build.sh
```
Ensure that your `~/.local/bin` directory is located in your PATH variables. 

#### Ubuntu 16.04
Since older versions of Ubuntu may not be adaptable to install the latest dependencies, you must first install the latest versions of the required dependencies:
```
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
sudo apt-add-repository 'deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-6.0 main'
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install clang-6.0 libstdc++-8-dev
pip3 install meson ninja --user
CC=clang-6.0 CXX=clang++-6.0 INSTALL_PREFIX=~/.local ./build.sh
```
Similar to Ubuntu 18.04, ensure that your `~/.local/bin` directory is located in your PATH variables.

### Other Linux Subsystems
Due to the sheer number of Linux distributions, it is impossible to provide explicit direction for the installation for each distribution. However, a generic outline of the installation process is still possible.
First, you must ensure that your distribution is regularly updated and receives newer packages, quicker. If your distribution fits this criterion, you may begin to install dependencies:
1. Install the backend.
    - If you use an Nvidia graphics card, install CUDA and cuDNN for your distribution. 
    - If yuou use an AMD graphics card, install OpenCL. 
    - If you use OpenBLAS, install `libopenblas-dev` (or a similar OpenBLAS backend).
2. Install the other necessary dependencies: `ninja-build`, `meson`, and `libgtest-dev` (or similar dependencies based on your specific distribution).

### WSL 2.0
To use SACAI, the latest version of WSL is required. Open the command prompt and install WSL with the simple `wsl --install` command. To choose a specific distribution (most favorably Ubuntu), you can run the following command: `wsl -d <DistributionName>`.

Once WSL has been successfully installed, proceed with compiling the source code.

## Compiling Source
Once the prerequisites have been successfully installed, the compilation of source code becomes significantly minimized. Simply clone the repository (if you haven't done so already) and navigate to the appropriate base directory. Run the `./build.sh` command to compile the source and build the project. If the shell file emits an error, it is likely that some of the dependencies aren't correctly installed. 

Once the compilation of source is complete, the executable shell file should be located in the `/build/release` directory. To ensure the code has been compiled appropriately, run `./sacai --config=sacai.cfg` to run the program. The appropriate lc0 neural networks should be preinstalled. If the networks are not preinstalled, extract the zip file for this repository and copy the supplied weights from the [weights](weights/) folder to the `/release/` folder.
