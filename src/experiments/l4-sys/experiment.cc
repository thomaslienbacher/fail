#include <iostream>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "experiment.hpp"
#include "experimentInfo.hpp"
#include "UDIS86.hpp"
#include "campaign.hpp"

#include "sal/SALConfig.hpp"
#include "sal/SALInst.hpp"
#include "sal/Memory.hpp"
#include "sal/bochs/BochsRegister.hpp"
#include "sal/Listener.hpp"
#include "config/FailConfig.hpp"

#include "l4sys.pb.h"

// FIXME libudis86 test -- remove me
#include <udis86.h>
void foo() { ud_t x; ud_init(&x); }

using namespace std;
using namespace fail;

// Check if configuration dependencies are satisfied:
#if !defined(CONFIG_EVENT_BREAKPOINTS) || !defined(CONFIG_SR_RESTORE) || \
    !defined(CONFIG_SR_SAVE) || !defined(CONFIG_SUPPRESS_INTERRUPTS) || \
    !defined(CONFIG_EVENT_TRAP) || !defined(CONFIG_EVENT_IOPORT) || \
    !defined(CONFIG_EVENT_INTERRUPT)
#error This experiment needs: breakpoints, suppressed-interrupts, traps, I/O port and interrupt events, \
  save, and restore. Enable these in the configuration.
#endif

typedef struct __trace_instr_type {
	fail::address_t trigger_addr;
	unsigned bp_counter;
} trace_instr;

string output;
#if 0
//disabled (see "STEP 2" below)
vector<trace_instr> instr_list;
vector<trace_instr> alu_instr_list;
#endif
string golden_run;

string L4SysExperiment::sanitised(const string &in_str) {
	string result;
	int in_str_size = in_str.size();
	result.reserve(in_str_size);
	for (int idx = 0; idx < in_str_size; idx++) {
		char cur_char = in_str[idx];
		unsigned cur_char_value = static_cast<unsigned>(cur_char);
		// also exclude the delimiter (',')
		if (cur_char_value < 0x20 || cur_char_value > 0x7E || cur_char_value == ',') {
			char str_nr[5];
			sprintf(str_nr, "\\%03o", cur_char_value);
			result += str_nr;
		} else {
			result += cur_char;
		}
	}
	return result;
}

BaseListener* L4SysExperiment::waitIOOrOther(bool clear_output) {
	IOPortListener ev_ioport(0x3F8, true);
	BaseListener* ev = NULL;
	if (clear_output)
		output.clear();
	while (true) {
		simulator.addListener(&ev_ioport);
		ev = simulator.resume();
		simulator.removeListener(&ev_ioport);
		if (ev == &ev_ioport) {
			output += ev_ioport.getData();
		} else {
			break;
		}
	}
	return ev;
}

Bit32u L4SysExperiment::eipBiased() {
	BX_CPU_C *cpu_context = simulator.getCPUContext();
	Bit32u EIP = cpu_context->gen_reg[BX_32BIT_REG_EIP].dword.erx;
	return EIP + cpu_context->eipPageBias;
}

const Bit8u *L4SysExperiment::calculateInstructionAddress() {
	// pasted in from various nested Bochs functions and macros - I hope
	// they will not change too soon (as do the Bochs developers, probably)
	BX_CPU_C *cpu_context = simulator.getCPUContext();
	const Bit8u *result = cpu_context->eipFetchPtr + eipBiased();
	return result;
}

bx_bool L4SysExperiment::fetchInstruction(BX_CPU_C *instance,
		const Bit8u *instr, bxInstruction_c *iStorage) {
	unsigned remainingInPage = instance->eipPageWindowSize - eipBiased();
	int ret;

#if BX_SUPPORT_X86_64
	if (BX_CPU_THIS_PTR cpu_mode == BX_MODE_LONG_64)
	ret = instance->fetchDecode64(instr, iStorage, remainingInPage);
	else
#endif
	ret = instance->fetchDecode32(instr, iStorage, remainingInPage);

	if (ret < 0) {
		// handle instrumentation callback inside boundaryFetch
		instance->boundaryFetch(instr, remainingInPage, iStorage);
		return 0;
	}

	return 1;
}

void L4SysExperiment::logInjection(Logger &log,
		const L4SysExperimentData &param) {
	// explicit type assignment necessary before sending over output stream
	int id = param.getWorkloadID();
	int instr_offset = param.msg.instr_offset();
	int bit_offset = param.msg.bit_offset();
	int exp_type = param.msg.exp_type();
	address_t injection_ip = param.msg.injection_ip();

	log << "job " << id << " exp_type " << exp_type << endl;
	log << "inject @ ip " << injection_ip << " (offset " << dec << instr_offset
			<< ")" << " bit " << bit_offset << endl;
}

void L4SysExperiment::readFromFileToVector(std::ifstream &file,
		std::vector<trace_instr> &instr_list) {
	file >> hex;
	while (!file.eof()) {
		trace_instr curr_instr;
		file.read(reinterpret_cast<char*>(&curr_instr), sizeof(trace_instr));
		instr_list.push_back(curr_instr);
	}
	file.close();
}

void L4SysExperiment::singleStep() {
	BPSingleListener singlestepping_event(ANY_ADDR, L4SYS_ADDRESS_SPACE);
	simulator.addListener(&singlestepping_event);
	waitIOOrOther(false);
	simulator.removeListener(&singlestepping_event);
}

bool L4SysExperiment::run() {
	Logger log("L4Sys", false);
	BPSingleListener bp(0, L4SYS_ADDRESS_SPACE);
	srand(time(NULL));

	log << "startup" << endl;

	struct stat teststruct;
	// STEP 1: run until interesting function starts, and save state
	if (stat(L4SYS_STATE_FOLDER, &teststruct) == -1) {
		bp.setWatchInstructionPointer(L4SYS_FUNC_ENTRY);
		simulator.addListenerAndResume(&bp);

		log << "test function entry reached, saving state" << endl;
		log << "EIP = " << hex << bp.getTriggerInstructionPointer() << " or "
				<< simulator.getRegisterManager().getInstructionPointer()
				<< endl;
		simulator.save(L4SYS_STATE_FOLDER);
	}

	// STEP 2: determine instructions executed
#ifdef PREPARE_EXPERIMENT
	log << "restoring state" << endl;
	simulator.restore(L4SYS_STATE_FOLDER);
	log << "EIP = " << hex
	<< simulator.getRegisterManager().getInstructionPointer()
	<< endl;

	// make sure the timer interrupt doesn't disturb us
	simulator.addSuppressedInterrupt(0);

	int count;
	int ul = 0, kernel = 0;
	bp.setWatchInstructionPointer(ANY_ADDR);
	for (count = 0; bp.getTriggerInstructionPointer() != L4SYS_FUNC_EXIT; ++count) {
		simulator.addListenerAndResume(&bp);
		if(bp.getTriggerInstructionPointer() < 0xC0000000) {
			ul++;
		} else {
			kernel++;
		}
		// log << "EIP = " << hex << simulator.getRegisterManager().getInstructionPointer() << endl;
	}
	log << "test function calculation position reached after " << dec << count << " instructions; "
			<< "ul: " << ul << ", kernel: " << kernel << endl;
#else
#if 0
	// the files currently get too big.
	/* I do not really have a clever idea to solve this.
	 * You would probably need some kind of loop detection,
	 * but for the moment, I have to focus on different issues.
	 */
	if (stat(L4SYS_INSTRUCTION_LIST, &teststruct) == -1 || stat(L4SYS_ALU_INSTRUCTIONS, &teststruct) == -1) {
		log << "restoring state" << endl;
		simulator.restore(L4SYS_STATE_FOLDER);
		log << "EIP = " << hex
		<< simulator.getRegisterManager().getInstructionPointer()
		<< endl;

		// make sure the timer interrupt doesn't disturb us
		simulator.addSuppressedInterrupt(0);

		ofstream instr_list_file(L4SYS_INSTRUCTION_LIST, ios::out | ios::binary);
		ofstream alu_instr_file(L4SYS_ALU_INSTRUCTIONS, ios::out | ios::binary);
		bp.setWatchInstructionPointer(ANY_ADDR);

		map<address_t, unsigned> times_called_map;
		while (bp.getTriggerInstructionPointer() != L4SYS_FUNC_EXIT) {
			simulator.addListenerAndResume(&bp);
			//short sanity check
			address_t curr_addr = bp.getTriggerInstructionPointer();
			assert(
					curr_addr == simulator.getRegisterManager().getInstructionPointer());

			unsigned times_called = times_called_map[curr_addr];
			times_called++;
			times_called_map[curr_addr] = times_called;

			trace_instr new_instr;
			new_instr.trigger_addr = curr_addr;
			new_instr.bp_counter = times_called;
			instr_list.push_back(new_instr);

			instr_list_file.write(reinterpret_cast<char*>(&new_instr), sizeof(trace_instr));

			// ALU instructions

			// decode the instruction
			bxInstruction_c instr;
			fetchInstruction(simulator.getCPUContext(), calculateInstructionAddress(), &instr);
			// add it to a second list if it is an ALU instruction
			if(isALUInstruction(instr.getIaOpcode())) {
				alu_instr_list.push_back(new_instr);
				alu_instr_file.write(reinterpret_cast<char*>(&new_instr), sizeof(trace_instr));
			}
		}
		log << "saving instructions triggered during normal execution" << endl;
		alu_instr_file.close();
		instr_list_file.close();
	} else {
		ifstream instr_list_file(L4SYS_INSTRUCTION_LIST, ios::in | ios::binary);
		ifstream alu_instr_file(L4SYS_ALU_INSTRUCTIONS, ios::in | ios::binary);
		readFromFileToVector(instr_list_file, instr_list);
		readFromFileToVector(alu_instr_file, alu_instr_list);
	}
#endif

	// STEP 3: determine the output of a "golden run"
	if (stat(L4SYS_CORRECT_OUTPUT, &teststruct) == -1) {
		log << "restoring state" << endl;
		simulator.restore(L4SYS_STATE_FOLDER);
		log << "EIP = " << hex
				<< simulator.getRegisterManager().getInstructionPointer()
				<< endl;

		// make sure the timer interrupt doesn't disturb us
		simulator.addSuppressedInterrupt(0);

		ofstream golden_run_file(L4SYS_CORRECT_OUTPUT);
		bp.setWatchInstructionPointer(L4SYS_FUNC_EXIT);
		bp.setCounter(L4SYS_ITERATION_COUNT);
		simulator.addListener(&bp);
		BaseListener* ev = waitIOOrOther(true);
		if (ev == &bp) {
			golden_run.assign(output.c_str());
			golden_run_file << output.c_str();
			log << "Output successfully logged!" << endl;
		} else {
			log
					<< "Obviously, there is some trouble with the events registered - aborting simulation!"
					<< endl;
			golden_run_file.close();
			simulator.terminate(10);
		}
		simulator.clearListeners();
		bp.setCounter(1);
		log << "saving output generated during normal execution" << endl;
		golden_run_file.close();
	} else {
		ifstream golden_run_file(L4SYS_CORRECT_OUTPUT);

		golden_run.reserve(teststruct.st_size);

		golden_run.assign((istreambuf_iterator<char>(golden_run_file)),
				istreambuf_iterator<char>());

		golden_run_file.close();

		//the generated output probably has a similar length
		output.reserve(teststruct.st_size);
	}

	// STEP 4: The actual experiment.
	log << "restoring state" << endl;
	simulator.restore(L4SYS_STATE_FOLDER);

	log << "asking job server for experiment parameters" << endl;
	L4SysExperimentData param;
	if (!m_jc.getParam(param)) {
		log << "Dying." << endl;
		// communicate that we were told to die
		simulator.terminate(1);
	}

	int instr_offset = param.msg.instr_offset();
	int bit_offset = param.msg.bit_offset();
	int exp_type = param.msg.exp_type();

	bp.setWatchInstructionPointer(ANY_ADDR);
	bp.setCounter(instr_offset + 1);
	simulator.addListener(&bp);
	//and log the output
	waitIOOrOther(true);

	// note at what IP we will do the injection
	address_t injection_ip =
			simulator.getRegisterManager().getInstructionPointer();
	param.msg.set_injection_ip(injection_ip);

#if 0
	// temporarily out of order (see above)
	// sanity check (only works if we're working with an instruction trace)
	if (injection_ip != instr_list[instr_offset].trigger_addr) {
		stringstream ss;
		ss << "SANITY CHECK FAILED: " << injection_ip << " != "
		<< instr_list[instr_offset].trigger_addr << endl;
		log << ss.str();
		param.msg.set_resulttype(param.msg.UNKNOWN);
		param.msg.set_resultdata(injection_ip);
		param.msg.set_details(ss.str());

		simulator.clearListeners();
		m_jc.sendResult(param);
		simulator.terminate(20);
	}
#endif

	// inject
	if (exp_type == param.msg.GPRFLIP) {
		RegisterManager& rm = simulator.getRegisterManager();
		Register *ebx = rm.getRegister(RID_EBX);
		regdata_t data = ebx->getData();
		regdata_t newdata = data ^ (1 << bit_offset);
		ebx->setData(newdata);

		// do the logging in case everything worked out
		logInjection(log, param);
		log << "register data: 0x" << hex << ((int) data) << " -> 0x"
				<< ((int) newdata) << endl;
	} else if (exp_type == param.msg.IDCFLIP) {
		// this is a twisted one

		// initial definitions
		bxICacheEntry_c *cache_entry = simulator.getCPUContext()->getICacheEntry();
		unsigned length_in_bits = cache_entry->i->ilen() << 3;

		// get the instruction in plain text in inject the error there
		// Note: we need to fetch some extra bytes into the array
		// in case the faulty instruction is interpreted to be longer
		// than the original one
		Bit8u curr_instr_plain[MAX_INSTR_BYTES];
		const Bit8u *addr = calculateInstructionAddress();
		memcpy(curr_instr_plain, addr, MAX_INSTR_BYTES);

		// CampaignManager has no idea of the instruction length
		// (neither do we), therefore this small adaption
		bit_offset %= length_in_bits;
		param.msg.set_bit_offset(bit_offset);

		// do some access calculation
		int byte_index = bit_offset >> 3;
		Bit8u bit_index = bit_offset & 7;

		// apply the fault
		curr_instr_plain[byte_index] ^= 1 << bit_index;

		// decode the instruction
		bxInstruction_c bochs_instr;
		memset(&bochs_instr, 0, sizeof(bxInstruction_c));
		fetchInstruction(simulator.getCPUContext(), curr_instr_plain,
				&bochs_instr);

		// inject it
		// backup the current and insert the faulty instruction
		bxInstruction_c old_instr;
		memcpy(&old_instr, cache_entry->i, sizeof(bxInstruction_c));
		memcpy(cache_entry->i, &bochs_instr, sizeof(bxInstruction_c));

		// execute the faulty instruction, then return
		singleStep();

		//restore the old instruction
		memcpy(cache_entry->i, &old_instr, sizeof(bxInstruction_c));

		// do the logging
		logInjection(log, param);
	} else if (exp_type == param.msg.RATFLIP) {
		bxICacheEntry_c *cache_entry = simulator.getCPUContext()->getICacheEntry();
		Udis86 udis(calculateInstructionAddress(), cache_entry->i->ilen(), injection_ip);
		if (udis.fetchNextInstruction()) {
			ud_t _ud = udis.getCurrentState();

			/* start Bjoern Doebel's code (slightly modified) */
			/* ============================================== */
			unsigned opcount     = 0;
			unsigned operands[4] = { ~0U, ~0U, ~0U, ~0U };
			enum {
				RAT_IDX_MASK   = 0x0FF,
				RAT_IDX_OFFSET = 0x100
			};

			for (unsigned i = 0; i < 3; ++i) {
				/*
				 * Case 1: operand is a register
				 */
				if (_ud.operand[i].type == UD_OP_REG) {
					operands[opcount++] = i;
				} else if (_ud.operand[i].type == UD_OP_MEM) {
					/*
					 * Case 2: operand is memory op.
					 *
					 * In this case, we may have 2 registers involved for the
					 * index-scale address calculation.
					 */
					if (_ud.operand[i].base != 0)  // 0 if hard-wired mem operand
						operands[opcount++] = i;
					if (_ud.operand[i].index != 0)
						operands[opcount++] = i + RAT_IDX_OFFSET;
				}
			}

			ud_type_t which;
			unsigned rnd;
			if(opcount == 0)
				rnd = 0;
			else
				rnd = rand() % opcount;

			if (operands[rnd] > RAT_IDX_OFFSET) {
				which = _ud.operand[operands[rnd] - RAT_IDX_OFFSET].index;
			} else {
				which = _ud.operand[operands[rnd]].base;
			}
			/* ============================================ */
			/* end Bjoern Doebel's code (slightly modified) */

			if (which != UD_NONE) {
				// so we are able to flip the associated registers
				// for details on the algorithm, see Bjoern Doebel's SWIFI/RATFlip class

				// some declarations
				GPRegisterId bochs_reg = Udis86::udisGPRToFailBochsGPR(which);
				int exchg_reg = -1;
				RegisterManager &rm = simulator.getRegisterManager();

				// first, decide if the fault hits a register bound to this thread
				// (ten percent chance)
				if (rand() % 10) {
					// assure exchange of registers
					exchg_reg = rand() % 7;
					if (exchg_reg == bochs_reg)
						exchg_reg++;

				}

				// prepare the fault
				regdata_t data = rm.getRegister(bochs_reg)->getData();
				if (rnd > 0) {
					//input register - do the fault injection here
					regdata_t newdata = 0;
					if(exchg_reg >= 0) {
						newdata = rm.getRegister(exchg_reg)->getData();
					} else {
						newdata = rand();
					}
					rm.getRegister(bochs_reg)->setData(newdata);
				}

				// execute the instruction
				singleStep();

				// restore
				if (rnd == 0) {
					// output register - do the fault injection here
					if(exchg_reg >= 0) {
						// write the result into the wrong local register
						regdata_t newdata = rm.getRegister(bochs_reg)->getData();
						rm.getRegister(exchg_reg)->setData(newdata);
					}
				}
				// restore the actual value of the register
				// in reality, it would never have been overwritten
				rm.getRegister(bochs_reg)->setData(data);
			}

		}
	} else if (exp_type == param.msg.ALUINSTR) {
	}

	// aftermath
	BPSingleListener ev_done(L4SYS_FUNC_EXIT, L4SYS_ADDRESS_SPACE);
	ev_done.setCounter(L4SYS_ITERATION_COUNT);
	simulator.addListener(&ev_done);
	const unsigned instr_run = L4SYS_ITERATION_COUNT * L4SYS_NUMINSTR;
	BPSingleListener ev_timeout(ANY_ADDR, L4SYS_ADDRESS_SPACE);
	ev_timeout.setCounter(instr_run + 3000);
	simulator.addListener(&ev_timeout);
	TrapListener ev_trap(ANY_TRAP);
	//one trap for each 150 instructions justifies an exception
	ev_trap.setCounter(instr_run / 150);
	simulator.addListener(&ev_trap);
	InterruptListener ev_intr(ANY_INTERRUPT);
	//one interrupt for each 100 instructions justifies an exception (timeout mostly)
	ev_intr.setCounter(instr_run / 100);
	simulator.addListener(&ev_intr);

	//do not discard output recorded so far
	BaseListener *ev = waitIOOrOther(false);

	/* copying a string object that contains control sequences
	 * unfortunately does not work with the library I am using,
	 * which is why output is passed on as C string and
	 * the string compare is done on C strings
	 */
	if (ev == &ev_done) {
		if (strcmp(output.c_str(), golden_run.c_str()) == 0) {
			log << dec << "Result DONE" << endl;
			param.msg.set_resulttype(param.msg.DONE);
		} else {
			log << dec << "Result WRONG" << endl;
			param.msg.set_resulttype(param.msg.WRONG);
			param.msg.set_output(sanitised(output.c_str()));
		}
	} else if (ev == &ev_timeout) {
		log << dec << "Result TIMEOUT" << endl;
		param.msg.set_resulttype(param.msg.TIMEOUT);
		param.msg.set_resultdata(
				simulator.getRegisterManager().getInstructionPointer());
		param.msg.set_output(sanitised(output.c_str()));
	} else if (ev == &ev_trap) {
		log << dec << "Result TRAP #" << ev_trap.getTriggerNumber() << endl;
		param.msg.set_resulttype(param.msg.TRAP);
		param.msg.set_resultdata(
				simulator.getRegisterManager().getInstructionPointer());
		param.msg.set_output(sanitised(output.c_str()));
	} else if (ev == &ev_intr) {
		log << hex << "Result INT FLOOD; Last INT #:"
				<< ev_intr.getTriggerNumber() << endl;
		param.msg.set_resulttype(param.msg.INTR);
		param.msg.set_resultdata(
				simulator.getRegisterManager().getInstructionPointer());
		param.msg.set_output(sanitised(output.c_str()));
	} else {
		log << dec << "Result WTF?" << endl;
		param.msg.set_resulttype(param.msg.UNKNOWN);
		param.msg.set_resultdata(
				simulator.getRegisterManager().getInstructionPointer());
		param.msg.set_output(sanitised(output.c_str()));

		stringstream ss;
		ss << "eventid " << ev << " EIP "
				<< simulator.getRegisterManager().getInstructionPointer()
				<< endl;
		param.msg.set_details(ss.str());
	}

	simulator.clearListeners();
	m_jc.sendResult(param);
#endif
#ifdef HEADLESS_EXPERIMENT
	simulator.terminate(0);
#endif

	// experiment successfully conducted
	return true;
}
