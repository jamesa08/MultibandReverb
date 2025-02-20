# MultibandReverb

Completed under Capital University Music Technology Program

# Usage
In the main repo directory execute
```
$ cmake -S . -B build
$ cmake --build build

# Build with 12 cores
$ cmake --build build -j12 

```
The first run will take the most time because the dependencies (CPM, JUCE, and googletest) need to be downloaded.

Run:
```
$ ./build/plugin/MultibandReverb_artefacts/Standalone/MultibandReverb.app/Contents/MacOS/MultibandReverb
```