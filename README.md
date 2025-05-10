Weather Routing Plugin for OpenCPN
===========================================

Perform weather routing, see "data/WeatherRoutingInformation.html"

Compiling
=========

* git clone git://github.com/rgleason/weather_routing_pi.git

Under windows, you must find the file "opencpn.lib" (Visual Studio) or "libopencpn.dll.a" (mingw) which is built in the build directory after compiling opencpn.  This file must be copied to the plugin directory.

Build as normally:

* cd ..
* cd build
* cmake ..
* make
* make install

For OSX standalone build in weather_routing_pi directory:

* mkdir build
* cd build
* cmake ..
* make
* make create-pkg

Unit Testing
============

The unit tests are disabled by default. To run the tests, define the relevant environment variable and run the tests as follows (from the build directory, as usual):

```
cmake -DOCPN_BUILD_TEST=ON ..
make
make test
```

You should see something like this:

```
Running tests...
/opt/homebrew/Cellar/cmake/3.30.5/bin/ctest --force-new-ctest-process
Test project weather_routing_pi/build
      Start  1: PolarTests.AssertionsBasic
 1/53 Test  #1: PolarTests.AssertionsBasic .....................   Passed    0.07 sec
      Start  2: PolarTests.ConstructorBasic
 2/53 Test  #2: PolarTests.ConstructorBasic ....................   Passed    0.01 sec
      Start  3: PolarTests.OpenFailed
 3/53 Test  #3: PolarTests.OpenFailed ..........................   Passed    0.01 sec
      Start  4: PolarTests.OpenSuccess
 4/53 Test  #4: PolarTests.OpenSuccess .........................   Passed    0.03 sec
 ...
 ```

All tests are intended to pass.  If any tests fail, please report an issue, providing enough context 
for a developer to reproduce and fix the problem.

License
=======
The plugin code is licensed under the terms of the GPL v3+ 

Part of the icons made by Smashicons (https://www.flaticon.com/authors/smashicons) from Flaticon (https://www.flaticon.com/) and is licensed under CC BY 3.0 (http://creativecommons.org/licenses/by/3.0)

