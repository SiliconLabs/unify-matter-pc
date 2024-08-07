# Unify Matter PC Unit Testing

The Matter PC test infrastructure leverages the test ecosystem of Matter itself.
Writing a unit test is done with the NL Unit test framework and Matter helper
functions.

## Build the docker container (host compilation)

```bash
dev-machine:~$ docker build -t unify-matter-host --build-arg ARCH=amd64 docker/
```

Starting the docker:

```bash
dev-machine:~$ docker run -it -v $PWD:/unify-matter-pc -v $PWD/linux/third_party/UnifySDK:/uic unify-matter-host
```

Compile and install libunify for host.

```bash
root@docker:/uic$ cmake -DCMAKE_INSTALL_PREFIX=$PWD/stage -GNinja -B build_unify_amd64/ -S components
root@docker:/uic$ cmake --build build_unify_amd64
root@docker:/uic$ cmake --install build_unify_amd64 --prefix $PWD/stage
root@docker:/uic$ export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$PWD/stage/share/pkgconfig
```

If you want to be able to use Zap to generate code from Unify XML files you need
to export UCL_XML_PATH as well.

```bash
root@docker:/uic$ export UCL_XML_PATH=/uic/components/uic_dotdot/dotdot-xml
```

Activate Matter development environment.

Make sure you are in `/unify-matter-pc/linux/third_party/connectedhomeip` directory

```bash
root@docker:/unify-matter-pc$ cd /unify-matter-pc/linux/third_party/connectedhomeip
root@docker:/unify-matter-pc/linux/third_party/connectedhomeip$ git config --global --add safe.directory /unify-matter-pc/linux/third_party/connectedhomeip
root@docker:/unify-matter-pc/linux/third_party/connectedhomeip$ git config --global --add safe.directory /unify-matter-pc/linux/third_party/connectedhomeip/third_party/pigweed/repo
root@docker:/unify-matter-pc/linux/third_party/connectedhomeip$ source ./scripts/activate.sh
```

## Compiling the unit test

Make sure you are in `/unify-matter-pc/linux/` directory

Before compiling a unit test, use the following GN command to generate the
ninja files necessary for the build process:

```bash
/unify-matter-pc/linux$ gn gen out/host --args='chip_build_tests=true use_coverage=true'
```

After generating the ninja files, the tests are generated with the following
command:

```bash
silabs_examples/unify-matter-pc/linux$ ninja -C out/host check
```

This generates test binaries inside `out/host/tests` where test executables can
be found.

## Running the unit test

Unit tests are run by first locating your test binary and then executing it individually.

```bash
matter/silabs_examples/unify-matter-pc/linux$ ./out/host/tests/TestExample
'#0:','ExampleTests'
'#3:','Example::TestExample','PASSED'
'#6:','0','1'
'#7:','0','1'
```
OR

All unit tests can be run together using
```bash
root@docker:/unify-matter-pc/linux$ ../scripts/run_tests.sh -b out/host
```

## Adding new unit test cases
Please refer to [Test example](../linux/src/tests/TestExample.cpp) for an example of how
to write a simple unit test. After writing a unit test remember to add the
source file in the [BUILD.gn](../linux/src/tests/BUILD.gn).

## Getting unit test coverage

To understand unit test coverage, run the following helper script:

If the unit test cases are built and run as per above section,
```bash
root@docker:/unify-matter-pc/linux$ ../scripts/get_test_coverage.sh -b out/host
```
If test cases are not built and run yet,
```bash
root@docker:/unify-matter-pc/linux$ ../scripts/get_test_coverage.sh -t amd64
```
