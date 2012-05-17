#include <iostream>

#include "SAL/SALInst.hpp"
#include "SAL/Register.hpp"
#include "experiment.hpp"
#include "plugins/tracing/TracingPlugin.hpp"

/*
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/gzip_stream.h>
*/

using std::cout;
using std::endl;
using namespace fi;
using namespace sal;

bool TracingTest::run()
{
	cout << "[TracingTest] Setting up experiment" << endl;

#if 1
	// STEP 1: run until interesting function starts, and save state
	BPEvent breakpoint(0x00101658);
	simulator.addEventAndWait(&breakpoint);
	cout << "[TracingTest] main() reached, saving" << endl;

	simulator.save("state");
#else
	// STEP 2: test tracing plugin
	simulator.restore("state");

	cout << "[TracingTest] enabling tracing" << endl;

	TracingPlugin tp;
	tp.setOstream(&cout);
	Trace trace;
	tp.setTraceMessage(&trace);
	// this must be done *after* configuring the plugin:
	simulator.addFlow(&tp);

	cout << "[TracingTest] tracing 1000000 instructions" << endl;
	BPEvent timeout(fi::ANY_ADDR);
	timeout.setCounter(1000000);
	simulator.addEvent(&timeout);

	InterruptEvent ie(fi::ANY_INTERRUPT);
	while (simulator.addEventAndWait(&ie) != &timeout) {
		cout << "INTERRUPT #" << ie.getTriggerNumber() << "\n";
	}

	cout << "[TracingTest] disabling tracing (trace size: "
	     << std::dec << trace.ByteSize() << " bytes)\n";
	simulator.removeFlow(&tp);

/*
	// serialize trace to file
	std::ofstream of("trace.pb");
	if (of.fail()) { return false; }
	trace.SerializeToOstream(&of);
	of.close();

	// serialize trace to gzip-compressed file
	int fd = open("trace.pb.gz", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (!fd) { return false; }
	google::protobuf::io::FileOutputStream fo(fd);
	google::protobuf::io::GzipOutputStream::Options options;
	options.compression_level = 9;
	google::protobuf::io::GzipOutputStream go(&fo, options);
	trace.SerializeToZeroCopyStream(&go);
	go.Close();
	fo.Close();
*/
#endif
	cout << "[TracingTest] Finished." << endl;
	simulator.clearEvents(this);
	simulator.terminate();

	return true;
}
