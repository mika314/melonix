# melonix

[![How hard is it to make melodyne?](https://user-images.githubusercontent.com/1877406/169781760-fb738b6f-0ae8-414f-ad4f-8d48a7e5f5ac.png)](https://youtu.be/qwAjW5hI148 "How hard is it to make melodyne?")

## Building instructions on Ubuntu 22.04

```bash
# install dependencies
sudo apt-get install -y clang pkg-config libfftw3-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libsdl2-dev git
# clone and compile build tool `coddle`
git clone https://github.com/coddle-cpp/coddle.git && cd coddle && ./build.sh
# install `coddle`
sudo ./deploy.sh
cd ..
# clone and build melonix
git clone https://github.com/mika314/melonix.git && cd melonix && coddle
# run
./melonix
```
