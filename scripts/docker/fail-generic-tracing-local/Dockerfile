# Inherit from docker container that has the fail source code prepared,
# including all tools which are needed to build FAIL*
FROM thomaslienbacher/fail-base-local

USER fail

ENV PATH /home/fail/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
WORKDIR /home/fail/fail
RUN mkdir build-tracer; cd build-tracer;
WORKDIR build-tracer

RUN cmake -DBUILD_IMPORT_TRACE=ON -DBUILD_PRUNE_TRACE=ON -DBUILD_LLVM_DISASSEMBLER=ON -DBUILD_CONVERT_TRACE=ON -DBUILD_DUMP_TRACE=ON -DCMAKE_AGPP_FLAGS:STRING="-D__NO_MATH_INLINES --c_compiler clang++-10" -DEXPERIMENTS_ACTIVATED:STRING="generic-tracing" -DPLUGINS_ACTIVATED:STRING="tracing;serialoutput" -DCMAKE_BUILD_TYPE:STRING="Debug" -DCMAKE_CXX_FLAGS:STRING="-O0 -g -DNDEBUG" -DCMAKE_C_FLAGS:STRING="-O0 -g -DNDEBUG" ..

# Make FAIL*, we need to call make multiple times due to race conditions 
RUN make -j $(nproc) || make -j $(nproc) || make -j $(nproc) || make -j $(nproc) 
RUN make -j $(nproc)
RUN make

RUN ln -s /home/fail/fail/build-tracer/bin/fail-client    /home/fail/bin/fail-x86-tracing;    \
    ln -s /home/fail/fail/build-tracer/bin/import-trace   /home/fail/bin/;                    \
    ln -s /home/fail/fail/build-tracer/bin/prune-trace    /home/fail/bin/;                    \
    ln -s /home/fail/fail/build-tracer/bin/dump-trace     /home/fail/bin/;                    \
    ln -s /home/fail/fail/build-tracer/bin/convert-trace  /home/fail/bin/;                    \
    cp /home/fail/fail/tools/bochs-experiment-runner/bochs-experiment-runner.py  /home/fail/bin/bochs-experiment-runner.py; \
    chmod a+x /home/fail/bin/bochs-experiment-runner.py;

USER root
