#ifndef __L4SYS_INSTRUCTIONFILTER_HPP__
  #define __L4SYS_INSTRUCTIONFILTER_HPP__

#include "sal/SALConfig.hpp"
#include "cpu/instr.h"

using namespace fail;

/**
 * \class InstructionFilter
 *
 * \brief Filters instructions
 *
 * This class is an interface that can be used to
 * implement instruction filter classes that can
 * be used to decide if an instruction should be
 * included in the fault injection experiment.
 */
class InstructionFilter {
public:
	/**
	 * Decides if the given instruction at the given program location
	 * is valid for fault injection
	 * @param ip the instruction pointer of the instruction
	 * @param instr the instruction in its coded binary representation
	 * @returns \c true if the instruction should be included, \c false otherwise
	 */
	virtual bool isValidInstr(address_t ip, char const *instr) const = 0;
	virtual ~InstructionFilter() {}
};

/**
 * \class RangeInstructionFilter
 *
 * Permits an instruction if its instruction pointer lies within a certain range
 */
class RangeInstructionFilter : public InstructionFilter {
public:
	RangeInstructionFilter(address_t begin_addr, address_t end_addr)
	: beginAddress(begin_addr), endAddress(end_addr)
	{}
	~RangeInstructionFilter() {}
	/**
	 * check if the instruction pointer of an instruction lies within a certain range
	 * @param ip the instruction pointer to check
	 * @param instr this parameter is ignored
	 * @returns \c true if the instruction lies within the predefined range,
	 *          \c false otherwise
	 */
	bool isValidInstr(address_t ip, char const *instr) const
	{ return (beginAddress <= ip && ip <= endAddress); }
private:
	address_t beginAddress; //<! the beginning of the address range
	address_t endAddress; //<! the end of the address range
};

#endif /* __L4SYS_INSTRUCTIONFILTER_HPP__ */
