#include <sstream>

#include "BochsController.hpp"
#include "BochsMemory.hpp"
#include "BochsRegister.hpp"
#include "../Register.hpp"
#include "../SALInst.hpp"

namespace fail {

#ifdef DANCEOS_RESTORE
bx_bool restore_bochs_request = false;
bx_bool save_bochs_request    = false;
std::string  sr_path          = "";
#endif

bx_bool reboot_bochs_request        = false;
bx_bool interrupt_injection_request = false;
int     interrupt_to_fire           = -1;

BochsController::BochsController()
	: SimulatorController(new BochsRegisterManager(), new BochsMemoryManager()),
	  m_CPUContext(NULL), m_CacheEntry(NULL)
{
	// -------------------------------------
	// Add the general purpose register:
  #if BX_SUPPORT_X86_64
	// -- 64 bit register --
  	const  std::string names[] = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI",
  	                               "RDI", "R8", "R9", "R10", "R11", "R12", "R13",
  	                               "R14", "R15" };
	for(unsigned short i = 0; i < 16; i++)
	{
		BxGPReg* pReg = new BxGPReg(i, 64, &(BX_CPU(0)->gen_reg[i].rrx));
		pReg->setName(names[i]);
		m_Regs->add(pReg);
	}
  #else
  	// -- 32 bit register --
  	const std::string names[] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI",
							      "EDI" };
	for(unsigned short i = 0; i < 8; i++)
	{
		BxGPReg* pReg = new BxGPReg(i, 32, &(BX_CPU(0)->gen_reg[i].dword.erx));
		pReg->setName(names[i]);
		m_Regs->add(pReg);
	}
  #endif // BX_SUPPORT_X86_64
  #ifdef DEBUG
	m_Regularity = 0; // disabled
	m_Counter = 0;
	m_pDest = NULL;
  #endif
	// -------------------------------------
	// Add the Program counter register:
  #if BX_SUPPORT_X86_64
	BxPCReg* pPCReg = new BxPCReg(RID_PC, 64, &(BX_CPU(0)->gen_reg[BX_64BIT_REG_RIP].rrx));
	pPCReg->setName("RIP");
  #else
    BxPCReg* pPCReg = new BxPCReg(RID_PC, 32, &(BX_CPU(0)->gen_reg[BX_32BIT_REG_EIP].dword.erx));
	pPCReg->setName("EIP");
  #endif // BX_SUPPORT_X86_64
    // -------------------------------------
	// Add the Status register (x86 cpu FLAGS):
	BxFlagsReg* pFlagReg = new BxFlagsReg(RID_FLAGS, reinterpret_cast<regdata_t*>(&(BX_CPU(0)->eflags)));
	// Note: "eflags" is (always) of type Bit32u which matches the regdata_t only in
	//       case of the 32 bit version (id est !BX_SUPPORT_X86_64). Therefor we need
	//       to ensure to assign only 32 bit to the Bochs internal register variable
	//       (see SAL/bochs/BochsRegister.hpp, setData) if we are in 64 bit mode.
	pFlagReg->setName("FLAGS");
	m_Regs->add(pFlagReg);
	m_Regs->add(pPCReg);
}

BochsController::~BochsController()
{
	for(RegisterManager::iterator it = m_Regs->begin(); it != m_Regs->end(); it++)
			delete (*it); // free the memory, allocated in the constructor
	m_Regs->clear();
	delete m_Regs;
	delete m_Mem;
}

#ifdef DEBUG
void BochsController::dbgEnableInstrPtrOutput(unsigned regularity, std::ostream* dest)
{
	m_Regularity = regularity;
	m_pDest = dest;
	m_Counter = 0;
}
#endif // DEBUG

void BochsController::onInstrPtrChanged(address_t instrPtr, address_t address_space,
		BX_CPU_C *context, bxICacheEntry_c *cache_entry)
{
#ifdef DEBUG
	if(m_Regularity != 0 && ++m_Counter % m_Regularity == 0)
		(*m_pDest) << "0x" << std::hex << instrPtr;
#endif
	m_CPUContext = context;
	m_CacheEntry = cache_entry;
	bool do_fire = false;
	// Check for active breakpoint-events:
	bp_cache_t &buffer_cache = m_EvList.getBPBuffer();
	bp_cache_t::iterator it = buffer_cache.begin();
	while(it != buffer_cache.end())
	{
		BPEvent* pEvBreakpt = *it;
		if(pEvBreakpt->isMatching(instrPtr, address_space))
		{
			pEvBreakpt->setTriggerInstructionPointer(instrPtr);
			it = buffer_cache.makeActive(m_EvList, it);
			do_fire = true;
			// "it" has already been set to the next element (by calling
			// makeActive()):
			continue; // -> skip iterator increment
		}
		it++;
	}
	if(do_fire)
		m_EvList.fireActiveEvents();
	// Note: SimulatorController::onBreakpointEvent will not be invoked in this
	//       implementation.
}

void BochsController::onIOPortEvent(unsigned char data, unsigned port, bool out) {
	// Check for active breakpoint-events:
	io_cache_t &buffer_cache = m_EvList.getIOBuffer();
	io_cache_t::iterator it = buffer_cache.begin();
	while(it != buffer_cache.end())
	{
		IOPortEvent* pIOPt = (*it);
		if(pIOPt->isMatching(port, out))
		{
			pIOPt->setData(data);
			it = buffer_cache.makeActive(m_EvList, it);
			// "it" has already been set to the next element (by calling
			// makeActive()):
			continue; // -> skip iterator increment
		}
		it++;
	}
	m_EvList.fireActiveEvents();
	// Note: SimulatorController::onBreakpointEvent will not be invoked in this
	//       implementation.
}

void BochsController::save(const std::string& path)
{
	int stat;
	
	stat = mkdir(path.c_str(), 0777);
	if(!(stat == 0 || errno == EEXIST))
		std::cout << "[FAIL] Can not create target-directory to save!" << std::endl;
		// TODO: (Non-)Verbose-Mode? Log-level? Maybe better: use return value to indicate failure?
	
	save_bochs_request = true;
	sr_path = path;
	m_CurrFlow = m_Flows.getCurrent();
	m_Flows.resume();
}

void BochsController::saveDone()
{
	save_bochs_request = false;
	m_Flows.toggle(m_CurrFlow);
}

void BochsController::restore(const std::string& path)
{
	clearEvents();
	restore_bochs_request = true;
	sr_path = path;
	m_CurrFlow = m_Flows.getCurrent();
	m_Flows.resume();
}

void BochsController::restoreDone()
{
	restore_bochs_request = false;
	m_Flows.toggle(m_CurrFlow);
}

void BochsController::reboot()
{
	clearEvents();
	reboot_bochs_request = true;
	m_CurrFlow = m_Flows.getCurrent();
	m_Flows.resume();
}

void BochsController::rebootDone()
{
	reboot_bochs_request = false;
	m_Flows.toggle(m_CurrFlow);
}

void BochsController::fireInterrupt(unsigned irq)
{
	interrupt_injection_request = true;
	interrupt_to_fire = irq;
	m_CurrFlow = m_Flows.getCurrent();
	m_Flows.resume();
}

void BochsController::fireInterruptDone()
{
	interrupt_injection_request = false;
	m_Flows.toggle(m_CurrFlow);
}

void BochsController::onTimerTrigger(void* thisPtr)
{
	// FIXME: The timer logic can be modified to use only one timer in Bochs.
	//        (For now, this suffices.)
	TimerEvent* pTmEv = static_cast<TimerEvent*>(thisPtr);
	// Check for a matching TimerEvent. (In fact, we are only
	// interessted in the iterator pointing at pTmEv.):
	EventList::iterator it = std::find(simulator.m_EvList.begin(),
											simulator.m_EvList.end(), pTmEv);
	// TODO: This has O(|m_EvList|) time complexity. We can further improve this
	//       by creating a method such that makeActive(pTmEv) works as well,
	//       reducing the time complexity to O(1).
	simulator.m_EvList.makeActive(it);
	simulator.m_EvList.fireActiveEvents();
}

const std::string& BochsController::getMnemonic() const
{
	static std::string str;
#if 0
	bxICacheEntry_c* pEntry = BX_CPU(0)->getICacheEntry();
	assert(pEntry != NULL && "FATAL ERROR: Bochs internal function returned NULL (not expected)!");
	bxInstruction_c* pInstr = pEntry->i;
	assert(pInstr != NULL && "FATAL ERROR: Bochs internal member was NULL (not expected)!");
#endif
	const char* pszName = get_bx_opcode_name(getICacheEntry()->i->getIaOpcode());
	if (pszName != NULL)
		str = pszName;
	else
		str.clear();
	return str;
}

} // end-of-namespace: fail
