#if 0
		// temporarily disabled to make the code in the repository compile - will soon be fixed
#include "sal/bochs/BochsController.hpp"
#include "UDIS86.hpp"

using namespace fail;

Udis86::Udis86(const unsigned char *instr, size_t size) {
	// initialise the buffer
	unsigned char *udis_instr = static_cast<unsigned char*>(malloc(size));
	memcpy(udis_instr, instr, size);

	// initialise the internal data structure
	memset(&ud_obj, 0, sizeof(ud_t));
	ud_init(&ud_obj);

	// assign the buffer to the data structure
	ud_set_input_buffer(&ud_obj, udis_instr, size);

	// free the buffer
	free(udis_instr);
}

bool Udis86::fetchNextInstruction() {
	return (ud_disassemble(&ud_obj) > 0);
}

GPRegisterId Udis86::udisGPRToFailBochsGPR(ud_type_t udisReg) {
#define REG_CASE(REG) case UD_R_##REG: return RID_##REG
	switch (udisReg) {
#if BX_SUPPORT_X86_64 // 64 bit register id's:
	REG_CASE(RAX);
	REG_CASE(RCX);
	REG_CASE(RDX);
	REG_CASE(RBX);
	REG_CASE(RSP);
	REG_CASE(RBP);
	REG_CASE(RSI);
	REG_CASE(RDI);
	REG_CASE(R8);
	REG_CASE(R9);
	REG_CASE(R10);
	REG_CASE(R11);
	REG_CASE(R12);
	REG_CASE(R13);
	REG_CASE(R14);
	REG_CASE(R15);
#else
	REG_CASE(EAX);
	REG_CASE(ECX);
	REG_CASE(EDX);
	REG_CASE(EBX);
	REG_CASE(ESP);
	REG_CASE(EBP);
	REG_CASE(ESI);
	REG_CASE(EDI);
#endif
	default:
		return RID_LAST_GP_ID;
	}
#undef REG_CASE
}
#endif
