# RTSPCamera

Library for low-latency video streaming over rtsp

### Project dependencies
- [live555](http://live555.com/)
- [FFmpeg](https://ffmpeg.org/)
- [pybind11](https://github.com/pybind/pybind11)
- [OpenCV](https://opencv.org/) (optional)

### Build instructions (Linux)
- build live555 library
```
cd live555
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B /tmp/build/live555 --install-prefix /tmp/build/third_party
cmake --build /tmp/build/live555
cmake --install /tmp/build/live555
```

- build project itself
```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B /tmp/build/rtspcamera -DTHIRD_PARTY_DIR=/tmp/build/third_party
cmake --build /tmp/build/rtspcamera
```


### Build instructions (Windows)
- build live555 library
```
cd live555
mkdir build
cd build
cmake -S .. --install-prefix %CD%\..\..\third_party
cmake --build . --config Release
cmake --install . --config Release
```

- build project itself
```
mkdir build
cd build
cmake -S .. --install-prefix %CD%\..\third_party -DTHIRD_PARTY_DIR=%CD%\..\third_party
cmake --build . --config Release
```

## License

RTSPCamera is licensed under a 2-clause BSD license.
