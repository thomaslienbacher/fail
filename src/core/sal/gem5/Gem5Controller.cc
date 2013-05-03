#include "Gem5Controller.hpp"

#include "../Listener.hpp"

#include "Gem5Wrapper.hpp"

#include "base/trace.hh"
#include "debug/FailState.hh"
#include "sim/root.hh"

namespace fail {

void Gem5Controller::startup()
{
	// Assuming there is only one defined system should be sufficient for most cases. More systems
	// are only used for switching cpu model or caches during a simulation run.
	m_System = GetSystemObject();
	m_Mem = new Gem5MemoryManager(m_System);

	int numCtxs = GetNumberOfContexts(m_System);
	for (int i = 0; i < numCtxs; i++) {
		ConcreteCPU* cpu = new ConcreteCPU(GetCPUId(m_System, i), m_System);
		addCPU(cpu);
	}

	// TODO pass on command-line parameters
	SimulatorController::startup();
}

Gem5Controller::~Gem5Controller()
{
	std::vector<ConcreteCPU*>::iterator it = m_CPUs.begin();
	while (it != m_CPUs.end()) {
		delete *it;
		it = m_CPUs.erase(it);
	}
	delete m_Mem;
}

void Gem5Controller::save(const std::string &path)
{
	DPRINTF(FailState, "Saving state to %s.\n", path);
	
	Serializable::serializeAll(path);

	return true;
}

void Gem5Controller::restore(const std::string &path)
{
	// FIXME: not working currently
	Root* root = Root::root();
	Checkpoint cp(path);

	root->loadState(&cp);
}

// TODO: Implement reboot
void Gem5Controller::reboot()
{

}

} // end-of-namespace: fail
