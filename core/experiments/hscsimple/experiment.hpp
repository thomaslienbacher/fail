#ifndef __HSCSIMPLE_EXPERIMENT_HPP__
  #define __HSCSIMPLE_EXPERIMENT_HPP__

#include "controller/ExperimentFlow.hpp"

class hscsimpleExperiment : public fail::ExperimentFlow
{
	public:
		hscsimpleExperiment() { }
	
		bool run();
};

#endif // __HSCSIMPLE_EXPERIMENT_HPP__
