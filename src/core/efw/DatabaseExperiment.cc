#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <bitset>

#include <stdlib.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "sal/SALConfig.hpp"
#include "sal/Memory.hpp"
#include "sal/Listener.hpp"
#include "sal/FaultSpace.hpp"
#include "efw/DatabaseExperiment.hpp"
#include "comm/DatabaseCampaignMessage.pb.h"

#ifdef BUILD_LLVM_DISASSEMBLER
#  include "util/llvmdisassembler/LLVMtoFailTranslator.hpp"
#endif

//#define LOCAL

using namespace std;
using namespace fail;
using namespace google::protobuf;

// Check if configuration dependencies are satisfied:
#if !defined(CONFIG_EVENT_BREAKPOINTS) || !defined(CONFIG_SR_RESTORE)
     #error This experiment needs: breakpoints, restore. Enable these in the configuration.
#endif

DatabaseExperiment::~DatabaseExperiment()  {
	delete this->m_jc;
}

void DatabaseExperiment::redecodeCurrentInstruction() {
	#if defined BUILD_BOCHS
	/* Flush Instruction Caches and Prefetch queue */
	BX_CPU_C *cpu_context = simulator.getCPUContext();
	cpu_context->invalidate_prefetch_q();
	cpu_context->iCache.flushICacheEntries();

	guest_address_t pc = simulator.getCPU(0).getInstructionPointer();
	bxInstruction_c *currInstr = simulator.getCurrentInstruction();

	m_log << "REDECODE INSTRUCTION @ IP 0x" << std::hex << pc << endl;

	guest_address_t eipBiased = pc + cpu_context->eipPageBias;
	Bit8u instr_plain[15];

	MemoryManager& mm = simulator.getMemoryManager();
	for (unsigned i = 0; i < sizeof(instr_plain); ++i) {
		if (!mm.isMapped(pc + i)) {
			m_log << "REDECODE: 0x" << std::hex << pc+i << "UNMAPPED" << endl;
			// TODO: error?
			return;
		}
	}

	mm.getBytes(pc, sizeof(instr_plain), instr_plain);

	guest_address_t remainingInPage = cpu_context->eipPageWindowSize - eipBiased;
	int ret;
#if BX_SUPPORT_X86_64
	if (cpu_context->cpu_mode == BX_MODE_LONG_64)
		ret = cpu_context->fetchDecode64(instr_plain, currInstr, remainingInPage);
	else
#endif
		ret = cpu_context->fetchDecode32(instr_plain, currInstr, remainingInPage);
	if (ret < 0) {
		// handle instrumentation callback inside boundaryFetch
		cpu_context->boundaryFetch(instr_plain, remainingInPage, currInstr);
	}
#endif
}

unsigned DatabaseExperiment::injectFault(
        util::fsp::address_t data_address, address_t ip,
        unsigned bitpos, uint8_t mask,
        bool inject_burst,
        bool inject_registers, bool force_registers) {

    using namespace util;

    std::unique_ptr<fsp::element> target = m_fsp.decode(data_address);

    std::function<unsigned(unsigned)> injector;
    if(inject_burst) {
        m_log << "INJECTING BURST " << endl;
        injector = [mask] (unsigned val) -> unsigned {
            return val ^ (mask & 0xFF);
        };
    } else {
        m_log << "INJECTING BITFLIP (bitpos = " << bitpos << ") " << endl;
        // using mask here, should be unneeded due to the early abort in run()
        // do it anyway for safety's sake.
        injector = [bitpos,mask] (unsigned val) -> unsigned {
           return val ^ (mask & (1 << bitpos));
        };
    }
    injector_result result = target->inject(injector);

    m_log << hex << setw(2) << setfill('0')
        << std::showbase
        << "\tIP: " << ip << endl
        << "\tFAULT SITE: FSP " << data_address
        << " -> AREA " << target->get_area()->get_name() << " @ " << target->get_offset() << endl
        << "\telement: " << *target << endl
        << "\tvalue: 0x" << (int)result.original
        <<     " -> 0x" << (int)result.injected << endl;
	return result.original;

	//[> First 128 registers, TODO use LLVMtoFailTranslator::getMaxDataAddress() <]
	//if (data_address < (128 << 4) && inject_registers) {
//#ifdef BUILD_LLVM_DISASSEMBLER
		//// register FI
		//LLVMtoFailTranslator::reginfo_t reginfo =
			//LLVMtoFailTranslator::reginfo_t::fromDataAddress(data_address, 1);

		//value = LLVMtoFailTranslator::getRegisterContent(simulator.getCPU(0), reginfo);
		//if (inject_burst) {
			//injected_value = value ^ 0xff;
			//m_log << "INJECTING BURST at: REGISTER " << dec << reginfo.id
				//<< " bitpos " << (reginfo.offset + bitpos) << endl;
		//} else {
			//injected_value = value ^ (1 << bitpos);
			//m_log << "INJECTING BIT-FLIP at: REGISTER " << dec << reginfo.id
				//<< " bitpos " << (reginfo.offset + bitpos) << endl;
		//}
		//LLVMtoFailTranslator::setRegisterContent(simulator.getCPU(0), reginfo, injected_value);
		//#if defined BUILD_BOCHS
		//if (reginfo.id == RID_PC) {
			//// FIXME move this into the Bochs backend
			//m_log << "Redecode current instruction" << endl;
			//redecodeCurrentInstruction();
		//}
		//#endif
//#else
		//m_log << "ERROR: Not compiled with LLVM.  Enable BUILD_LLVM_DISASSEMBLER at buildtime." << endl;
		//simulator.terminate(1);
//#endif
	//} else if (!force_registers) {
		//// memory FI
		//value = m_mm.getByte(data_address);

		//if (inject_burst) {
			//injected_value = value ^ 0xff;
			//m_log << "INJECTING BURST at: MEM 0x"
				//<< hex << setw(2) << setfill('0') << data_address << endl;
		//} else {
			//injected_value = value ^ (1 << bitpos);
			//m_log << "INJECTING BIT-FLIP (" << dec << bitpos << ") at: MEM 0x"
				//<< hex << setw(2) << setfill('0') << data_address << endl;
		//}
		//m_mm.setByte(data_address, injected_value);

	//} else {
		//m_log << "WARNING: Skipping injection, data address 0x"
			//<< hex << data_address << " out of range." << endl;
		//return 0;
	//}
}

template<class T>
T * protobufFindSubmessageByTypename(Message *msg, const std::string &name) {
	T * submessage = 0;
	const Descriptor *msg_type = msg->GetDescriptor();
	const Message::Reflection *ref = msg->GetReflection();
	const Descriptor *database_desc =
		DescriptorPool::generated_pool()->FindMessageTypeByName(name);
	assert(database_desc != 0);

	size_t count = msg_type->field_count();

	for (unsigned i = 0; i < count; i++) {
		const FieldDescriptor *field = msg_type->field(i);
		assert(field != 0);
		if (field->message_type() == database_desc) {
			submessage = dynamic_cast<T*>(ref->MutableMessage(msg, field));
			assert(submessage != 0);
			break;
		}
	}
	return submessage;
}


bool DatabaseExperiment::run()
{
    m_log << "STARTING EXPERIMENT" << endl;

	if (!this->cb_start_experiment()) {
		m_log << "Initialization failed. Exiting." << endl;
		simulator.terminate(1);
	}

	unsigned executed_jobs = 0;
	unsigned max_executed_jobs = 25;
	#ifdef BUILD_SAIL
		max_executed_jobs = UINT_MAX; // Sail emulators dont leak memory, they dont have to be restarted
	#endif
	while (executed_jobs < max_executed_jobs || m_jc->getNumberOfUndoneJobs() > 0) {
		m_log << "asking jobserver for parameters" << endl;
		ExperimentData * param = this->cb_allocate_experiment_data();
#ifndef LOCAL
		if (!m_jc->getParam(*param)){
			m_log << "Dying." << endl; // We were told to die.
			simulator.terminate(1);
		}
#endif
		m_current_param = param;

		DatabaseCampaignMessage * fsppilot =
			protobufFindSubmessageByTypename<DatabaseCampaignMessage>(&param->getMessage(), "DatabaseCampaignMessage");
		assert (fsppilot != 0);

#ifdef LOCAL
		fsppilot->set_injection_instr(0);
		fsppilot->set_injection_instr_absolute(1048677);
		fsppilot->set_data_address(2101240);
		fsppilot->set_data_width(1);
		fsppilot->set_inject_bursts(true);
#endif

		unsigned  injection_instr = fsppilot->injection_instr();

        fail::util::fsp::address_t data_address = fsppilot->data_address();

        unsigned unchecked_mask = fsppilot->data_mask();
        assert(unchecked_mask <=255 && "mask covers more than 8 bit, this is unsupported!");
        uint8_t mask = static_cast<uint8_t>(unchecked_mask);
		unsigned injection_width = fsppilot->inject_bursts() ? 8 : 1;

        m_log << std::hex << std::showbase
            << " fsp: " << data_address
            << " mask: " << std::bitset<8>(mask)
            << std::dec
            << " injection_width: " << injection_width << std::endl;

		for (unsigned bit_offset = 0; bit_offset < 8; bit_offset += injection_width) {
            // if the mask is zero at this bit offset, this bit shall not be injected.
            bool allowed_mask = mask & (1 << bit_offset);
            // additionally, always inject once for bursts.
            // this first bit might be unset otherwise and thus,
            // this address will never be injected otherwise.
            if(!(allowed_mask || fsppilot->inject_bursts())) {
                continue;
            }

			// 8 results in one job
			Message *outer_result = cb_new_result(param);
			m_current_result = outer_result;
			DatabaseExperimentMessage *result =
				protobufFindSubmessageByTypename<DatabaseExperimentMessage>(outer_result, "DatabaseExperimentMessage");
			result->set_bitoffset(bit_offset);
			m_log << "restoring state" << endl;
			// Restore to the image, which starts at address(main)
			simulator.restore(cb_state_directory());
			executed_jobs ++;

			m_log << "Trying to inject @ instr #" << dec << injection_instr << endl;

			simulator.clearListeners(this);

			if (!this->cb_before_fast_forward()) {
				continue;
			}

			// Do we need to fast-forward at all?
			fail::BaseListener *listener = 0;
			if (injection_instr > 0) {
				// Create a listener that matches any IP event. It is used to
				// forward to the injection point.
				BPSingleListener bp;
				bp.setWatchInstructionPointer(ANY_ADDR);
				bp.setCounter(injection_instr);
				simulator.addListener(&bp);

				while (true) {
					listener = simulator.resume();
					if (listener == &bp) {
						break;
					} else {
						bool should_continue = this->cb_during_fast_forward(listener);
						if (!should_continue)
							break; // Stop fast forwarding
					}
				}
			}
			if (!this->cb_after_fast_forward(listener)) {
				continue; // Continue to next injection experiment
			}

			address_t injection_instr_absolute = fsppilot->injection_instr_absolute();
			bool found_eip = false;
			for (size_t i = 0; i < simulator.getCPUCount(); i++) {
				address_t eip = simulator.getCPU(i).getInstructionPointer();
				if (eip == injection_instr_absolute) {
					found_eip = true;
				}
			}
			if (fsppilot->has_injection_instr_absolute() && !found_eip) {
				m_log << "Invalid Injection address  != 0x" << std::hex << injection_instr_absolute<< std::endl;
				for (size_t i = 0; i < simulator.getCPUCount(); i++) {
					address_t eip = simulator.getCPU(i).getInstructionPointer();
					m_log << " CPU " << i << " EIP = 0x" << std::hex << eip << std::dec << std::endl;
				}
				simulator.terminate(1);
			}

			simulator.clearListeners(this);

			// inject fault (single-bit flip or burst)
			result->set_original_value(
				injectFault(data_address, injection_instr_absolute, bit_offset, mask,
					fsppilot->inject_bursts(),
					fsppilot->register_injection_mode() != fsppilot->OFF,
					fsppilot->register_injection_mode() == fsppilot->FORCE));
			result->set_injection_width(injection_width);

			if (!this->cb_before_resume()) {
				continue; // Continue to next experiment
			}

			m_log << "Resuming till the crash" << std::endl;
			// resume and wait for results
			while (true) {
				listener = simulator.resume();
				bool should_continue = this->cb_during_resume(listener);
				if (!should_continue)
					break;
			}
			m_log << "Resume done" << std::endl;
			this->cb_after_resume(listener);

			simulator.clearListeners(this);
		}
#ifndef LOCAL
		m_jc->sendResult(*param);
#else
		break;
#endif
		this->cb_free_experiment_data(param);
	}
	// Explicitly terminate, or the simulator will continue to run.
	simulator.terminate();
	return false;
}


