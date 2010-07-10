/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $URL$
 * $Id$
 *
 */

#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/stack.h"
#include "common/config-manager.h"

#include "sci/sci.h"
#include "sci/console.h"
#include "sci/debug.h"	// for g_debugState
#include "sci/resource.h"
#include "sci/engine/features.h"
#include "sci/engine/state.h"
#include "sci/engine/kernel.h"
#include "sci/engine/script.h"
#include "sci/engine/seg_manager.h"
#include "sci/engine/selector.h"	// for SELECTOR
#include "sci/engine/gc.h"

namespace Sci {

const reg_t NULL_REG = {0, 0};
const reg_t SIGNAL_REG = {0, SIGNAL_OFFSET};
const reg_t TRUE_REG = {0, 1};
//#define VM_DEBUG_SEND

#define SCI_XS_CALLEE_LOCALS ((SegmentId)-1)

#define END Script_None

opcode_format g_opcode_formats[128][4] = {
	/*00*/
	{Script_None}, {Script_None}, {Script_None}, {Script_None},
	/*04*/
	{Script_None}, {Script_None}, {Script_None}, {Script_None},
	/*08*/
	{Script_None}, {Script_None}, {Script_None}, {Script_None},
	/*0C*/
	{Script_None}, {Script_None}, {Script_None}, {Script_None},
	/*10*/
	{Script_None}, {Script_None}, {Script_None}, {Script_None},
	/*14*/
	{Script_None}, {Script_None}, {Script_None}, {Script_SRelative, END},
	/*18*/
	{Script_SRelative, END}, {Script_SRelative, END}, {Script_SVariable, END}, {Script_None},
	/*1C*/
	{Script_SVariable, END}, {Script_None}, {Script_None}, {Script_Variable, END},
	/*20*/
	{Script_SRelative, Script_Byte, END}, {Script_Variable, Script_Byte, END}, {Script_Variable, Script_Byte, END}, {Script_Variable, Script_SVariable, Script_Byte, END},
	/*24 (24=ret)*/
	{Script_End}, {Script_Byte, END}, {Script_Invalid}, {Script_Invalid},
	/*28*/
	{Script_Variable, END}, {Script_Invalid}, {Script_Byte, END}, {Script_Variable, Script_Byte, END},
	/*2C*/
	{Script_SVariable, END}, {Script_SVariable, Script_Variable, END}, {Script_None}, {Script_Invalid},
	/*30*/
	{Script_None}, {Script_Property, END}, {Script_Property, END}, {Script_Property, END},
	/*34*/
	{Script_Property, END}, {Script_Property, END}, {Script_Property, END}, {Script_Property, END},
	/*38*/
	{Script_Property, END}, {Script_SRelative, END}, {Script_SRelative, END}, {Script_None},
	/*3C*/
	{Script_None}, {Script_None}, {Script_None}, {Script_Word},
	/*40-4F*/
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	/*50-5F*/
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	/*60-6F*/
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	/*70-7F*/
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END},
	{Script_Global, END}, {Script_Local, END}, {Script_Temp, END}, {Script_Param, END}
};
#undef END

// TODO: script_adjust_opcode_formats should probably be part of the
// constructor (?) of a VirtualMachine or a ScriptManager class.
void script_adjust_opcode_formats() {
	if (g_sci->_features->detectLofsType() != SCI_VERSION_0_EARLY) {
		g_opcode_formats[op_lofsa][0] = Script_Offset;
		g_opcode_formats[op_lofss][0] = Script_Offset;
	}

#ifdef ENABLE_SCI32
	// In SCI32, some arguments are now words instead of bytes
	if (getSciVersion() >= SCI_VERSION_2) {
		g_opcode_formats[op_calle][2] = Script_Word;
		g_opcode_formats[op_callk][1] = Script_Word;
		g_opcode_formats[op_super][1] = Script_Word;
		g_opcode_formats[op_send][0] = Script_Word;
		g_opcode_formats[op_self][0] = Script_Word;
		g_opcode_formats[op_call][1] = Script_Word;
		g_opcode_formats[op_callb][1] = Script_Word;
	}
#endif
}

/**
 * Adds an entry to the top of the execution stack.
 *
 * @param[in] s				The state with which to execute
 * @param[in] pc			The initial program counter
 * @param[in] sp			The initial stack pointer
 * @param[in] objp			Pointer to the beginning of the current object
 * @param[in] argc			Number of parameters to call with
 * @param[in] argp			Heap pointer to the first parameter
 * @param[in] selector		The selector by which it was called or
 *							NULL_SELECTOR if n.a. For debugging.
 * @param[in] exportId		The exportId by which it was called or
 *							-1 if n.a. For debugging.
 * @param[in] sendp			Pointer to the object which the message was
 * 							sent to. Equal to objp for anything but super.
 * @param[in] origin		Number of the execution stack element this
 * 							entry was created by (usually the current TOS
 * 							number, except for multiple sends).
 * @param[in] local_segment	The segment to use for local variables,
 *							or SCI_XS_CALLEE_LOCALS to use obj's segment.
 * @return 					A pointer to the new exec stack TOS entry
 */
static ExecStack *add_exec_stack_entry(Common::List<ExecStack> &execStack, reg_t pc, StackPtr sp,
		reg_t objp, int argc, StackPtr argp, Selector selector, int exportId, int localCallOffset,
		reg_t sendp, int origin, SegmentId local_segment);


/**
 * Adds one varselector access to the execution stack.
 * This function is called from send_selector only.
 * @param[in] s			The EngineState to use
 * @param[in] objp		Pointer to the object owning the selector
 * @param[in] argc		1 for writing, 0 for reading
 * @param[in] argp		Pointer to the address of the data to write -2
 * @param[in] selector	Selector name
 * @param[in] address	Heap address of the selector
 * @param[in] origin	Stack frame which the access originated from
 * @return 				Pointer to the new exec-TOS element
 */
static ExecStack *add_exec_stack_varselector(Common::List<ExecStack> &execStack, reg_t objp, int argc,
		StackPtr argp, Selector selector, const ObjVarRef& address,
		int origin);




// validation functionality

static reg_t &validate_property(Object *obj, int index) {
	// A static dummy reg_t, which we return if obj or index turn out to be
	// invalid. Note that we cannot just return NULL_REG, because client code
	// may modify the value of the returned reg_t.
	static reg_t dummyReg = NULL_REG;

	// FIXME/TODO: Where does this occur? Returning a dummy reg here could lead
	// to all sorts of issues! Turned it into an error for now...
	// If this occurs, it means there's probably something wrong with the garbage
	// collector, so don't hide it with fake return values
	if (!obj) {
		error("validate_property: Sending to disposed object");
		//return dummyReg;
	}

	if (index < 0 || (uint)index >= obj->getVarCount()) {
		// This is same way sierra does it and there are some games, that contain such scripts like
		//  iceman script 998 (fred::canBeHere, executed right at the start)
		debugC(2, kDebugLevelVM, "[VM] Invalid property #%d (out of [0..%d]) requested!",
			index, obj->getVarCount());
		return dummyReg;
	}

	return obj->getVariableRef(index);
}

static StackPtr validate_stack_addr(EngineState *s, StackPtr sp) {
	if (sp >= s->stack_base && sp < s->stack_top)
		return sp;

	error("[VM] Stack index %d out of valid range [%d..%d]",
		(int)(sp - s->stack_base), 0, (int)(s->stack_top - s->stack_base - 1));
	return 0;
}

static int validate_arithmetic(reg_t reg) {
	if (reg.segment) {
		// The results of this are likely unpredictable... It most likely means that a kernel function is returning something wrong.
		// If such an error occurs, we usually need to find the last kernel function called and check its return value. Check
		// callKernelFunc() below
		error("[VM] Attempt to read arithmetic value from non-zero segment [%04x]. Address: %04x:%04x", reg.segment, PRINT_REG(reg));
		return 0;
	}

	return reg.offset;
}

static int signed_validate_arithmetic(reg_t reg) {
	return (int16)validate_arithmetic(reg);
}

static bool validate_variable(reg_t *r, reg_t *stack_base, int type, int max, int index) {
	const char *names[4] = {"global", "local", "temp", "param"};

	if (index < 0 || index >= max) {
		Common::String txt = Common::String::printf(
							"[VM] Attempt to use invalid %s variable %04x ",
							names[type], index);
		if (max == 0)
			txt += "(variable type invalid)";
		else
			txt += Common::String::printf("(out of range [%d..%d])", 0, max - 1);

		if (type == VAR_PARAM || type == VAR_TEMP) {
			int total_offset = r - stack_base;
			if (total_offset < 0 || total_offset >= VM_STACK_SIZE) {
				// Fatal, as the game is trying to do an OOB access
				error("%s. [VM] Access would be outside even of the stack (%d); access denied", txt.c_str(), total_offset);
				return false;
			} else {
				// WORKAROUND: Mixed-Up Mother Goose tries to use an invalid parameter in Event::new().
				// Just skip around it here so we don't error out in validate_arithmetic.
				if (g_sci->getGameId() == GID_MOTHERGOOSE && getSciVersion() <= SCI_VERSION_1_1 && type == VAR_PARAM && index == 1)
					return false;

				debugC(2, kDebugLevelVM, "%s", txt.c_str());
				debugC(2, kDebugLevelVM, "[VM] Access within stack boundaries; access granted.");
				return true;
			}
		}
		return false;
	}

	return true;
}

struct SciTrackOriginReply {
	int scriptNr;
	Common::String objectName;
	Common::String methodName;
	int localCallOffset;
};

static reg_t trackOriginAndFindWorkaround(int index, const SciWorkaroundEntry *workaroundList, SciTrackOriginReply *trackOrigin) {
	EngineState *state = g_sci->getEngineState();
	ExecStack *lastCall = state->xs;
	Script *local_script = state->_segMan->getScriptIfLoaded(lastCall->local_segment);
	int curScriptNr = local_script->getScriptNumber();

	if (lastCall->debugLocalCallOffset != -1) {
		// if lastcall was actually a local call search back for a real call
		Common::List<ExecStack>::iterator callIterator = state->_executionStack.end();
		while (callIterator != state->_executionStack.begin()) {
			callIterator--;
			ExecStack loopCall = *callIterator;
			if ((loopCall.debugSelector != -1) || (loopCall.debugExportId != -1)) {
				lastCall->debugSelector = loopCall.debugSelector;
				lastCall->debugExportId = loopCall.debugExportId;
				break;
			}
		}
	}

	Common::String curObjectName = state->_segMan->getObjectName(lastCall->sendp);
	Common::String curMethodName;
	const SciGameId gameId = g_sci->getGameId();

	if (lastCall->type == EXEC_STACK_TYPE_CALL) {
		if (lastCall->debugSelector != -1) {
			curMethodName = g_sci->getKernel()->getSelectorName(lastCall->debugSelector);
		} else if (lastCall->debugExportId != -1) {
			curObjectName = "";
			curMethodName = curMethodName.printf("export %d", lastCall->debugExportId);
		}
	}

	if (workaroundList) {
		// Search if there is a workaround for this one
		const SciWorkaroundEntry *workaround;
		int16 inheritanceLevel = 0;
		Common::String searchObjectName = curObjectName;
		reg_t searchObject = lastCall->sendp;
		do {
			workaround = workaroundList;
			while (workaround->objectName) {
				if (workaround->gameId == gameId && workaround->scriptNr == curScriptNr && (workaround->inheritanceLevel == inheritanceLevel) && (workaround->objectName == searchObjectName)
						&& workaround->methodName == curMethodName && workaround->localCallOffset == lastCall->debugLocalCallOffset && workaround->index == index) {
					// Workaround found
					return workaround->newValue;
				}
				workaround++;
			}

			// Go back to the parent
			inheritanceLevel++;
			searchObject = state->_segMan->getObject(searchObject)->getSuperClassSelector();
			if (!searchObject.isNull())
				searchObjectName = state->_segMan->getObjectName(searchObject);
		} while (!searchObject.isNull()); // no parent left?
	}

	// give caller origin data
	trackOrigin->objectName = curObjectName;
	trackOrigin->methodName = curMethodName;
	trackOrigin->scriptNr = curScriptNr;
	trackOrigin->localCallOffset = lastCall->debugLocalCallOffset;
	return make_reg(0xFFFF, 0xFFFF);
}

//    gameID,       scriptNr,lvl,         object-name, method-name,    call, index,   replace
static const SciWorkaroundEntry uninitializedReadWorkarounds[] = {
	{ GID_LAURABOW2,      24,  0,              "gcWin", "open",           -1,    5, { 0, 0xf } }, // is used as priority for game menu
	{ GID_FREDDYPHARKAS,  24,  0,              "gcWin", "open",           -1,    5, { 0, 0xf } }, // is used as priority for game menu
	{ GID_FREDDYPHARKAS,  31,  0,            "quitWin", "open",           -1,    5, { 0, 0xf } }, // is used as priority for game menu
	{ GID_GK2,            11,  0,                   "", "export 10",      -1,    3, { 0,   0 } }, // called when the game starts
	{ GID_JONES,         232,  0,        "weekendText", "draw",        0x3d3,    0, { 0,   0 } }, // jones/cd only - gets called during the game
	{ GID_JONES,         255,  0,                   "", "export 0",       -1,   13, { 0,   0 } }, // jones/ega&vga only - called when the game starts
	{ GID_JONES,         255,  0,                   "", "export 0",       -1,   14, { 0,   0 } }, // jones/ega&vga only - called when the game starts
	{ GID_LSL1,          720,  0,              "rm720", "init",           -1,    0, { 0,   0 } }, // age check room
	{ GID_LSL3,          997,  0,         "TheMenuBar", "handleEvent",    -1,    1, { 0, 0xf } }, // when setting volume the first time, this temp is used to set volume on entry (normally it would have been initialized to 's')
	{ GID_LSL6,          928,  1,           "Narrator", "startText",      -1,    0, { 0,   0 } }, // used by various objects that are even translated in foreign versions, that's why we use the base-class
	{ GID_ISLANDBRAIN,   140,  0,              "piece", "init",           -1,    3, { 0,   1 } }, // first puzzle right at the start, some initialization variable. bnt is done on it, and it should be non-0
	{ GID_ISLANDBRAIN,   268,  0,          "anElement", "select",         -1,    0, { 0,   0 } }, // elements puzzle, gets used before super TextIcon
	{ GID_KQ5,             0,  0,                   "", "export 29",      -1,    3, { 0,   0 } }, // called when playing harp for the harpies, is used for kDoAudio
	{ GID_KQ5,            25,  0,              "rm025", "doit",           -1,    0, { 0,   0 } }, // inside witch forest, where the walking rock is
	{ GID_KQ6,           903,  0,         "controlWin", "open",           -1,    4, { 0,   0 } }, // when opening the controls window (save, load etc)
	{ GID_SQ1,           703,  0,                   "", "export 1",       -1,    0, { 0,   0 } }, // sub that's called from several objects while on sarien battle cruiser
	{ GID_SQ1,           703,  0,         "firePulsar", "changeState", 0x18a,    0, { 0,   0 } }, // export 1, but called locally (when shooting at aliens)
	{ GID_SQ4,           928,  0,           "Narrator", "startText",      -1, 1000, { 0,   1 } }, // sq4cd: method returns this to the caller
	{ GID_SQ6,             0,  0,                "SQ6", "init",           -1,    2, { 0,   0 } }, // called when the game starts
	{ GID_SQ6,         64950,  0,               "View", "handleEvent",    -1,    0, { 0,   0 } }, // called when pressing "Start game" in the main menu
	SCI_WORKAROUNDENTRY_TERMINATOR
};

static reg_t validate_read_var(reg_t *r, reg_t *stack_base, int type, int max, int index, reg_t default_value) {
	if (validate_variable(r, stack_base, type, max, index)) {
		if (type == VAR_TEMP && r[index].segment == 0xffff) {
			// Uninitialized read on a temp
			//  We need to find correct replacements for each situation manually
			SciTrackOriginReply originReply;
			r[index] = trackOriginAndFindWorkaround(index, uninitializedReadWorkarounds, &originReply);
			if ((r[index].segment == 0xFFFF) && (r[index].offset == 0xFFFF))
				error("Uninitialized read for temp %d from method %s::%s (script %d, localCall %x)", index, originReply.objectName.c_str(), originReply.methodName.c_str(), originReply.scriptNr, originReply.localCallOffset);
		}
		return r[index];
	} else
		return default_value;
}

static void validate_write_var(reg_t *r, reg_t *stack_base, int type, int max, int index, reg_t value, SegManager *segMan, Kernel *kernel) {
	if (validate_variable(r, stack_base, type, max, index)) {

		// WORKAROUND: This code is needed to work around a probable script bug, or a
		// limitation of the original SCI engine, which can be observed in LSL5.
		//
		// In some games, ego walks via the "Grooper" object, in particular its "stopGroop"
		// child. In LSL5, during the game, ego is swapped from Larry to Patti. When this
		// happens in the original interpreter, the new actor is loaded in the same memory
		// location as the old one, therefore the client variable in the stopGroop object
		// points to the new actor. This is probably why the reference of the stopGroop
		// object is never updated (which is why I mentioned that this is either a script
		// bug or some kind of limitation).
		//
		// In our implementation, each new object is loaded in a different memory location,
		// and we can't overwrite the old one. This means that in our implementation,
		// whenever ego is changed, we need to update the "client" variable of the
		// stopGroop object, which points to ego, to the new ego object. If this is not
		// done, ego's movement will not be updated properly, so the result is
		// unpredictable (for example in LSL5, Patti spins around instead of walking).
		if (index == 0 && type == VAR_GLOBAL && getSciVersion() > SCI_VERSION_0_EARLY) {	// global 0 is ego
			reg_t stopGroopPos = segMan->findObjectByName("stopGroop");
			if (!stopGroopPos.isNull()) {	// does the game have a stopGroop object?
				// Find the "client" member variable of the stopGroop object, and update it
				ObjVarRef varp;
				if (lookupSelector(segMan, stopGroopPos, SELECTOR(client), &varp, NULL) == kSelectorVariable) {
					reg_t *clientVar = varp.getPointer(segMan);
					*clientVar = value;
				}
			}
		}

		// If we are writing an uninitialized value into a temp, we remove the uninitialized segment
		//  this happens at least in sq1/room 44 (slot-machine), because a send is missing parameters, then
		//  those parameters are taken from uninitialized stack and afterwards they are copied back into temps
		//  if we don't remove the segment, we would get false-positive uninitialized reads later
		if (type == VAR_TEMP && value.segment == 0xffff)
			value.segment = 0;

		r[index] = value;
	}
}

#define READ_VAR(type, index) validate_read_var(s->variables[type], s->stack_base, type, s->variablesMax[type], index, s->r_acc)
#define WRITE_VAR(type, index, value) validate_write_var(s->variables[type], s->stack_base, type, s->variablesMax[type], index, value, s->_segMan, g_sci->getKernel())
#define WRITE_VAR16(type, index, value) WRITE_VAR(type, index, make_reg(0, value));

#define ACC_ARITHMETIC_L(op) make_reg(0, (op validate_arithmetic(s->r_acc)))

// Operating on the stack
// 16 bit:
#define PUSH(v) PUSH32(make_reg(0, v))
#define POP() (validate_arithmetic(POP32()))
// 32 bit:
#define PUSH32(a) (*(validate_stack_addr(s, (s->xs->sp)++)) = (a))
#define POP32() (*(validate_stack_addr(s, --(s->xs->sp))))

ExecStack *execute_method(EngineState *s, uint16 script, uint16 pubfunct, StackPtr sp, reg_t calling_obj, uint16 argc, StackPtr argp) {
	int seg = s->_segMan->getScriptSegment(script);
	Script *scr = s->_segMan->getScriptIfLoaded(seg);

	if (!scr || scr->isMarkedAsDeleted()) { // Script not present yet?
		seg = s->_segMan->instantiateScript(script);
		scr = s->_segMan->getScript(seg);
	}

	const int temp = scr->validateExportFunc(pubfunct);
	if (!temp) {
#ifdef ENABLE_SCI32
		// HACK: Temporarily switch to a warning in SCI32 games until we can figure out why Torin has
		// an invalid exported function.
		if (getSciVersion() >= SCI_VERSION_2)
			warning("Request for invalid exported function 0x%x of script 0x%x", pubfunct, script);
		else
#endif
			error("Request for invalid exported function 0x%x of script 0x%x", pubfunct, script);
		return NULL;
	}

	// Check if a breakpoint is set on this method
	if (g_debugState._activeBreakpointTypes & BREAK_EXPORT) {
		uint32 bpaddress;

		bpaddress = (script << 16 | pubfunct);

		Common::List<Breakpoint>::const_iterator bp;
		for (bp = g_debugState._breakpoints.begin(); bp != g_debugState._breakpoints.end(); ++bp) {
			if (bp->type == BREAK_EXPORT && bp->address == bpaddress) {
				Console *con = g_sci->getSciDebugger();
				con->DebugPrintf("Break on script %d, export %d\n", script, pubfunct);
				g_debugState.debugging = true;
				g_debugState.breakpointWasHit = true;
				break;
			}
		}
	}

	return add_exec_stack_entry(s->_executionStack, make_reg(seg, temp), sp, calling_obj, argc, argp, -1, pubfunct, -1, calling_obj, s->_executionStack.size()-1, seg);
}


static void _exec_varselectors(EngineState *s) {
	// Executes all varselector read/write ops on the TOS
	while (!s->_executionStack.empty() && s->_executionStack.back().type == EXEC_STACK_TYPE_VARSELECTOR) {
		ExecStack &xs = s->_executionStack.back();
		reg_t *var = xs.getVarPointer(s->_segMan);
		if (!var) {
			error("Invalid varselector exec stack entry");
		} else {
			// varselector access?
			if (xs.argc) { // write?
				*var = xs.variables_argp[1];

			} else // No, read
				s->r_acc = *var;
		}
		s->_executionStack.pop_back();
	}
}

/** This struct is used to buffer the list of send calls in send_selector() */
struct CallsStruct {
	reg_t addr_func;
	reg_t varp_objp;
	union {
		reg_t func;
		ObjVarRef var;
	} address;
	StackPtr argp;
	int argc;
	Selector selector;
	StackPtr sp; /**< Stack pointer */
	int type; /**< Same as ExecStack.type */
};

ExecStack *send_selector(EngineState *s, reg_t send_obj, reg_t work_obj, StackPtr sp, int framesize, StackPtr argp) {
// send_obj and work_obj are equal for anything but 'super'
// Returns a pointer to the TOS exec_stack element
	assert(s);

	reg_t funcp;
	int selector;
	int argc;
	int origin = s->_executionStack.size()-1; // Origin: Used for debugging
	bool printSendActions = false;
	// We return a pointer to the new active ExecStack

	// The selector calls we catch are stored below:
	Common::Stack<CallsStruct> sendCalls;

	while (framesize > 0) {
		selector = validate_arithmetic(*argp++);
		argc = validate_arithmetic(*argp);

		if (argc > 0x800) { // More arguments than the stack could possibly accomodate for
			error("send_selector(): More than 0x800 arguments to function call");
		}

		// Check if a breakpoint is set on this method
		if (g_debugState._activeBreakpointTypes & BREAK_SELECTOR) {
			char method_name[256];

			sprintf(method_name, "%s::%s", s->_segMan->getObjectName(send_obj), g_sci->getKernel()->getSelectorName(selector).c_str());

			Common::List<Breakpoint>::const_iterator bp;
			for (bp = g_debugState._breakpoints.begin(); bp != g_debugState._breakpoints.end(); ++bp) {
				int cmplen = bp->name.size();
				if (bp->name.lastChar() != ':')
					cmplen = 256;

				if (bp->type == BREAK_SELECTOR && !strncmp(bp->name.c_str(), method_name, cmplen)) {
					Console *con = g_sci->getSciDebugger();
					con->DebugPrintf("Break on %s (in [%04x:%04x])\n", method_name, PRINT_REG(send_obj));
					printSendActions = true;
					g_debugState.debugging = true;
					g_debugState.breakpointWasHit = true;
					break;
				}
			}
		}

#ifdef VM_DEBUG_SEND
		printf("Send to %04x:%04x, selector %04x (%s):", PRINT_REG(send_obj), selector, g_sci->getKernel()->getSelectorName(selector).c_str());
#endif // VM_DEBUG_SEND

		ObjVarRef varp;
		switch (lookupSelector(s->_segMan, send_obj, selector, &varp, &funcp)) {
		case kSelectorNone:
			error("Send to invalid selector 0x%x of object at %04x:%04x", 0xffff & selector, PRINT_REG(send_obj));
			break;

		case kSelectorVariable:

#ifdef VM_DEBUG_SEND
			if (argc)
				printf("Varselector: Write %04x:%04x\n", PRINT_REG(argp[1]));
			else
				printf("Varselector: Read\n");
#endif // VM_DEBUG_SEND

			// argc == 0: read selector
			// argc != 0: write selector
			if (printSendActions && !argc) {	// read selector
				debug("[read selector]\n");
				printSendActions = false;
			}

			if (printSendActions && argc) {
				reg_t oldReg = *varp.getPointer(s->_segMan);
				reg_t newReg = argp[1];
				debug("[write to selector: change %04x:%04x to %04x:%04x]\n", PRINT_REG(oldReg), PRINT_REG(newReg));
				printSendActions = false;
			}

			if (argc > 1) {
				// argc can indeed be bigger than 1 in some cases, and it's usually the
				// result of a script bug

				const char *objectName = s->_segMan->getObjectName(send_obj);

				if (!strcmp(objectName, "Sq4GlobalNarrator") && selector == 606) {
					// SQ4 has a script bug in the Sq4GlobalNarrator object when invoking the
					// returnVal selector, which doesn't affect gameplay, thus don't diplay it
				} else if (!strcmp(objectName, "longSong") && selector == 3 && g_sci->getGameId() == GID_QFG1) {
					// QFG1VGA has a script bug in the longSong object when invoking the
					// loop selector, which doesn't affect gameplay, thus don't diplay it
				} else if (!strcmp(objectName, "PuzPiece") && selector == 77 && g_sci->getGameId() == GID_CASTLEBRAIN) {
					// Castle of Dr. Brain has a script bug in the PuzPiece object when invoking
					// the value selector, which doesn't affect gameplay, thus don't display it
				} else {
					// Unknown script bug, show it. Usually these aren't fatal.
					reg_t oldReg = *varp.getPointer(s->_segMan);
					reg_t newReg = argp[1];
					const char *selectorName = g_sci->getKernel()->getSelectorName(selector).c_str();
					warning("send_selector(): argc = %d while modifying variable selector "
							"%x (%s) of object %04x:%04x (%s) from %04x:%04x to %04x:%04x",
							argc, selector, selectorName, PRINT_REG(send_obj),
							objectName, PRINT_REG(oldReg), PRINT_REG(newReg));
				}
			}

			{
				CallsStruct call;
				call.address.var = varp; // register the call
				call.argp = argp;
				call.argc = argc;
				call.selector = selector;
				call.type = EXEC_STACK_TYPE_VARSELECTOR; // Register as a varselector
				sendCalls.push(call);
			}

			break;

		case kSelectorMethod:

#ifdef VM_DEBUG_SEND
			printf("Funcselector(");
			for (int i = 0; i < argc; i++) {
				printf("%04x:%04x", PRINT_REG(argp[i+1]));
				if (i + 1 < argc)
					printf(", ");
			}
			printf(") at %04x:%04x\n", PRINT_REG(funcp));
#endif // VM_DEBUG_SEND
			if (printSendActions) {
				printf("[invoke selector]");
#ifndef VM_DEBUG_SEND
				int displaySize = 0;
				for (int argNr = 1; argNr <= argc; argNr++) {
					if (argNr == 1)
						printf(" - ");
					reg_t curParam = argp[argNr];
					if (curParam.segment) {
						printf("[%04x:%04x] ", PRINT_REG(curParam));
						displaySize += 12;
					} else {
						printf("[%04x] ", curParam.offset);
						displaySize += 7;
					}
					if (displaySize > 50) {
						if (argNr < argc)
							printf("...");
						break;
					}
				}
#endif
				printf("\n");
				printSendActions = false;
			}

			{
				CallsStruct call;
				call.address.func = funcp; // register call
				call.argp = argp;
				call.argc = argc;
				call.selector = selector;
				call.type = EXEC_STACK_TYPE_CALL;
				call.sp = sp;
				sp = CALL_SP_CARRY; // Destroy sp, as it will be carried over
				sendCalls.push(call);
			}

			break;
		} // switch (lookupSelector())

		framesize -= (2 + argc);
		argp += argc + 1;
	}

	// Iterate over all registered calls in the reverse order. This way, the first call is
	// placed on the TOS; as soon as it returns, it will cause the second call to be executed.
	while (!sendCalls.empty()) {
		CallsStruct call = sendCalls.pop();
		if (call.type == EXEC_STACK_TYPE_VARSELECTOR) // Write/read variable?
			add_exec_stack_varselector(s->_executionStack, work_obj, call.argc, call.argp,
			                                    call.selector, call.address.var, origin);
		else
			add_exec_stack_entry(s->_executionStack, call.address.func, call.sp, work_obj,
			                         call.argc, call.argp,
			                         call.selector, -1, -1, send_obj, origin, SCI_XS_CALLEE_LOCALS);
	}

	_exec_varselectors(s);

	return s->_executionStack.empty() ? NULL : &(s->_executionStack.back());
}

static ExecStack *add_exec_stack_varselector(Common::List<ExecStack> &execStack, reg_t objp, int argc, StackPtr argp, Selector selector, const ObjVarRef& address, int origin) {
	ExecStack *xstack = add_exec_stack_entry(execStack, NULL_REG, 0, objp, argc, argp, selector, -1, -1, objp, origin, SCI_XS_CALLEE_LOCALS);
	// Store selector address in sp

	xstack->addr.varp = address;
	xstack->type = EXEC_STACK_TYPE_VARSELECTOR;

	return xstack;
}

static ExecStack *add_exec_stack_entry(Common::List<ExecStack> &execStack, reg_t pc, StackPtr sp, reg_t objp, int argc,
								   StackPtr argp, Selector selector, int exportId, int localCallOffset, reg_t sendp, int origin, SegmentId _localsSegment) {
	// Returns new TOS element for the execution stack
	// _localsSegment may be -1 if derived from the called object

	//printf("Exec stack: [%d/%d], origin %d, at %p\n", s->execution_stack_pos, s->_executionStack.size(), origin, s->execution_stack);

	ExecStack xstack;

	xstack.objp = objp;
	if (_localsSegment != SCI_XS_CALLEE_LOCALS)
		xstack.local_segment = _localsSegment;
	else
		xstack.local_segment = pc.segment;

	xstack.sendp = sendp;
	xstack.addr.pc = pc;
	xstack.fp = xstack.sp = sp;
	xstack.argc = argc;

	xstack.variables_argp = argp; // Parameters

	*argp = make_reg(0, argc);  // SCI code relies on the zeroeth argument to equal argc

	// Additional debug information
	xstack.debugSelector = selector;
	xstack.debugExportId = exportId;
	xstack.debugLocalCallOffset = localCallOffset;
	xstack.debugOrigin = origin;

	xstack.type = EXEC_STACK_TYPE_CALL; // Normal call

	execStack.push_back(xstack);
	return &(execStack.back());
}

static reg_t pointer_add(EngineState *s, reg_t base, int offset) {
	SegmentObj *mobj = s->_segMan->getSegmentObj(base.segment);

	if (!mobj) {
		error("[VM] Error: Attempt to add %d to invalid pointer %04x:%04x", offset, PRINT_REG(base));
		return NULL_REG;
	}

	switch (mobj->getType()) {

	case SEG_TYPE_LOCALS:
	case SEG_TYPE_SCRIPT:
	case SEG_TYPE_STACK:
	case SEG_TYPE_DYNMEM:
		base.offset += offset;
		return base;

	default:
		// FIXME: Changed this to warning, because iceman does this during dancing with girl.
		// Investigate why that is so and either fix the underlying issue or implement a more
		// specialized workaround!
		warning("[VM] Error: Attempt to add %d to pointer %04x:%04x, type %d: Pointer arithmetics of this type unsupported", offset, PRINT_REG(base), mobj->getType());
		return NULL_REG;

	}
}

static void addKernelCallToExecStack(EngineState *s, int kernelCallNr, int argc, reg_t *argv) {
	// Add stack frame to indicate we're executing a callk.
	// This is useful in debugger backtraces if this
	// kernel function calls a script itself.
	ExecStack *xstack;
	xstack = add_exec_stack_entry(s->_executionStack, NULL_REG, NULL, NULL_REG, argc, argv - 1, 0, -1, -1, NULL_REG,
			  s->_executionStack.size()-1, SCI_XS_CALLEE_LOCALS);
	xstack->debugSelector = kernelCallNr;
	xstack->type = EXEC_STACK_TYPE_KERNEL;
}

static void callKernelFunc(EngineState *s, int kernelCallNr, int argc) {
	Kernel *kernel = g_sci->getKernel();

	if (kernelCallNr >= (int)kernel->_kernelFuncs.size())
		error("Invalid kernel function 0x%x requested", kernelCallNr);

	const KernelFunction &kernelCall = kernel->_kernelFuncs[kernelCallNr];
	reg_t *argv = s->xs->sp + 1;

	if (kernelCall.signature
			&& !kernel->signatureMatch(kernelCall.signature, argc, argv)) {
		// signature mismatch, check if a workaround is available
		SciTrackOriginReply originReply;
		reg_t workaround;
		workaround = trackOriginAndFindWorkaround(0, kernelCall.workarounds, &originReply);
		if ((workaround.segment == 0xFFFF) && (workaround.offset == 0xFFFF)) {
			kernel->signatureDebug(kernelCall.signature, argc, argv);
			error("[VM] k%s[%x]: signature mismatch via method %s::%s (script %d, localCall %x)", kernelCall.name, kernelCallNr, originReply.objectName.c_str(), originReply.methodName.c_str(), originReply.scriptNr, originReply.localCallOffset);
		}
		// FIXME: implement some real workaround type logic - ignore call, still do call etc.
		if (workaround.segment == 2)
			s->r_acc = make_reg(0, workaround.offset);
		if (workaround.segment)
			return;
	}


	// Call kernel function
	if (!kernelCall.subFunctionCount) {
		addKernelCallToExecStack(s, kernelCallNr, argc, argv);
		s->r_acc = kernelCall.function(s, argc, argv);
	} else {
		// Sub-functions available, check signature and call that one directly
		if (argc < 1)
			error("[VM] k%s[%x]: no subfunction-id parameter given", kernelCall.name, kernelCallNr);
		const uint16 subId = argv[0].toUint16();
		// Skip over subfunction-id
		argc--;
		argv++;
		if (subId >= kernelCall.subFunctionCount)
			error("[VM] k%s: subfunction-id %d requested, but not available", kernelCall.name, subId);
		const KernelSubFunction &kernelSubCall = kernelCall.subFunctions[subId];
		if (kernelSubCall.signature && !kernel->signatureMatch(kernelSubCall.signature, argc, argv)) {
			// Signature mismatch
			SciTrackOriginReply originReply;
			reg_t workaround;
			workaround = trackOriginAndFindWorkaround(0, kernelSubCall.workarounds, &originReply);
			if ((workaround.segment == 0xFFFF) && (workaround.offset == 0xFFFF)) {
				kernel->signatureDebug(kernelSubCall.signature, argc, argv);
				int callNameLen = strlen(kernelCall.name);
				if (strncmp(kernelCall.name, kernelSubCall.name, callNameLen) == 0) {
					const char *subCallName = kernelSubCall.name + callNameLen;
					error("[VM] k%s(%s): signature mismatch via method %s::%s (script %d, localCall %x)", kernelCall.name, subCallName, originReply.objectName.c_str(), originReply.methodName.c_str(), originReply.scriptNr, originReply.localCallOffset);
				}
				error("[VM] k%s: signature mismatch via method %s::%s (script %d, localCall %x)", kernelSubCall.name, originReply.objectName.c_str(), originReply.methodName.c_str(), originReply.scriptNr, originReply.localCallOffset);
			}
			// FIXME: implement some real workaround type logic - ignore call, still do call etc.
			if (workaround.segment == 2)
				s->r_acc = make_reg(0, workaround.offset);
			if (workaround.segment)
				return;
		}
		if (!kernelSubCall.function)
			error("[VM] k%s: subfunction-id %d requested, but not available", kernelCall.name, subId);
		addKernelCallToExecStack(s, kernelCallNr, argc, argv);
		s->r_acc = kernelSubCall.function(s, argc, argv);
	}

#if 0
		// Used for debugging
		Common::String debugMsg = kernelFunc.origName +
									Common::String::printf("[0x%x]", kernelFuncNr) +
									Common::String::printf(", %d params: ", argc) +
									" (";

		for (int i = 0; i < argc; i++) {
			debugMsg +=  Common::String::printf("%04x:%04x", PRINT_REG(argv[i]));
			debugMsg += (i == argc - 1 ? ")" : ", ");
		}

		debugMsg += ", result: " + Common::String::printf("%04x:%04x", PRINT_REG(s->r_acc));
		debug("%s", debugMsg.c_str());
#endif

	// Remove callk stack frame again, if there's still an execution stack
	if (s->_executionStack.begin() != s->_executionStack.end())
		s->_executionStack.pop_back();
}

static void gcCountDown(EngineState *s) {
	if (s->gcCountDown-- <= 0) {
		s->gcCountDown = s->scriptGCInterval;
		run_gc(s);
	}
}

int readPMachineInstruction(const byte *src, byte &extOpcode, int16 opparams[4]) {
	uint offset = 0;
	extOpcode = src[offset++]; // Get "extended" opcode (lower bit has special meaning)
	const byte opcode = extOpcode >> 1;	// get the actual opcode

	memset(opparams, 0, sizeof(opparams));

	for (int i = 0; g_opcode_formats[opcode][i]; ++i) {
		//printf("Opcode: 0x%x, Opnumber: 0x%x, temp: %d\n", opcode, opcode, temp);
		assert(i < 4);
		switch (g_opcode_formats[opcode][i]) {

		case Script_Byte:
			opparams[i] = src[offset++];
			break;
		case Script_SByte:
			opparams[i] = (int8)src[offset++];
			break;

		case Script_Word:
			opparams[i] = READ_SCI11ENDIAN_UINT16(src + offset);
			offset += 2;
			break;
		case Script_SWord:
			opparams[i] = (int16)READ_SCI11ENDIAN_UINT16(src + offset);
			offset += 2;
			break;

		case Script_Variable:
		case Script_Property:

		case Script_Local:
		case Script_Temp:
		case Script_Global:
		case Script_Param:

		case Script_Offset:
			if (extOpcode & 1) {
				opparams[i] = src[offset++];
			} else {
				opparams[i] = READ_SCI11ENDIAN_UINT16(src + offset);
				offset += 2;
			}
			break;

		case Script_SVariable:
		case Script_SRelative:
			if (extOpcode & 1) {
				opparams[i] = (int8)src[offset++];
			} else {
				opparams[i] = (int16)READ_SCI11ENDIAN_UINT16(src + offset);
				offset += 2;
			}
			break;

		case Script_None:
		case Script_End:
			break;

		case Script_Invalid:
		default:
			error("opcode %02x: Invalid", extOpcode);
		}
	}

	return offset;
}

void run_vm(EngineState *s, bool restoring) {
	assert(s);

	int temp;
	reg_t r_temp; // Temporary register
	StackPtr s_temp; // Temporary stack pointer
	int16 opparams[4]; // opcode parameters

	s->restAdjust = 0;	// &rest adjusts the parameter count by this value
	// Current execution data:
	s->xs = &(s->_executionStack.back());
	ExecStack *xs_new = NULL;
	Object *obj = s->_segMan->getObject(s->xs->objp);
	Script *scr = 0;
	Script *local_script = s->_segMan->getScriptIfLoaded(s->xs->local_segment);
	int old_executionStackBase = s->executionStackBase;
	// Used to detect the stack bottom, for "physical" returns

	if (!local_script)
		error("run_vm(): program counter gone astray (local_script pointer is null)");

	if (!restoring)
		s->executionStackBase = s->_executionStack.size() - 1;

	s->variablesSegment[VAR_TEMP] = s->variablesSegment[VAR_PARAM] = s->_segMan->findSegmentByType(SEG_TYPE_STACK);
	s->variablesBase[VAR_TEMP] = s->variablesBase[VAR_PARAM] = s->stack_base;

	s->_executionStackPosChanged = true; // Force initialization

	while (1) {
		int var_type; // See description below
		int var_number;

		g_debugState.old_pc_offset = s->xs->addr.pc.offset;
		g_debugState.old_sp = s->xs->sp;

		if (s->abortScriptProcessing != kAbortNone || g_engine->shouldQuit())
			return; // Stop processing

		if (s->_executionStackPosChanged) {
			scr = s->_segMan->getScriptIfLoaded(s->xs->addr.pc.segment);
			if (!scr)
				error("No script in segment %d",  s->xs->addr.pc.segment);
			s->xs = &(s->_executionStack.back());
			s->_executionStackPosChanged = false;

			obj = s->_segMan->getObject(s->xs->objp);
			local_script = s->_segMan->getScriptIfLoaded(s->xs->local_segment);
			if (!local_script) {
				// FIXME: Why does this happen? Is the script not loaded yet at this point?
				warning("Could not find local script from segment %x", s->xs->local_segment);
				local_script = NULL;
				s->variablesBase[VAR_LOCAL] = s->variables[VAR_LOCAL] = NULL;
				s->variablesMax[VAR_LOCAL] = 0;
			} else {
				s->variablesSegment[VAR_LOCAL] = local_script->_localsSegment;
				if (local_script->_localsBlock)
					s->variablesBase[VAR_LOCAL] = s->variables[VAR_LOCAL] = local_script->_localsBlock->_locals.begin();
				else
					s->variablesBase[VAR_LOCAL] = s->variables[VAR_LOCAL] = NULL;
				if (local_script->_localsBlock)
					s->variablesMax[VAR_LOCAL] = local_script->_localsBlock->_locals.size();
				else
					s->variablesMax[VAR_LOCAL] = 0;
				s->variablesMax[VAR_TEMP] = s->xs->sp - s->xs->fp;
				s->variablesMax[VAR_PARAM] = s->xs->argc + 1;
			}
			s->variables[VAR_TEMP] = s->xs->fp;
			s->variables[VAR_PARAM] = s->xs->variables_argp;
		}

		if (s->abortScriptProcessing != kAbortNone || g_engine->shouldQuit())
			return; // Stop processing

		// Debug if this has been requested:
		// TODO: re-implement sci_debug_flags
		if (g_debugState.debugging /* sci_debug_flags*/) {
			script_debug(s);
			g_debugState.breakpointWasHit = false;
		}
		Console *con = g_sci->getSciDebugger();
		if (con->isAttached()) {
			con->onFrame();
		}

		if (s->xs->sp < s->xs->fp)
			error("run_vm(): stack underflow, sp: %04x:%04x, fp: %04x:%04x",
			PRINT_REG(*s->xs->sp), PRINT_REG(*s->xs->fp));

		s->variablesMax[VAR_TEMP] = s->xs->sp - s->xs->fp;

		if (s->xs->addr.pc.offset >= scr->getBufSize())
			error("run_vm(): program counter gone astray, addr: %d, code buffer size: %d",
			s->xs->addr.pc.offset, scr->getBufSize());

		// Get opcode
		byte extOpcode;
		s->xs->addr.pc.offset += readPMachineInstruction(scr->getBuf() + s->xs->addr.pc.offset, extOpcode, opparams);
		const byte opcode = extOpcode >> 1;

		switch (opcode) {

		case op_bnot: // 0x00 (00)
			// Binary not
			s->r_acc = ACC_ARITHMETIC_L(0xffff ^ /*acc*/);
			break;

		case op_add: // 0x01 (01)
			r_temp = POP32();

			 // Happens in SQ1, room 28, when throwing the water at Orat
			 if (s->r_acc.segment == 0xFFFF) {
				// WORKAROUND: init uninitialized variable to 0
				warning("op_add: attempt to write to uninitialized variable");
				s->r_acc = NULL_REG;
			 }

			if (r_temp.segment || s->r_acc.segment) {
				reg_t r_ptr = NULL_REG;
				int offset;
				// Pointer arithmetics!
				if (s->r_acc.segment) {
					if (r_temp.segment) {
						error("Attempt to add two pointers, stack=%04x:%04x and acc=%04x:%04x",
						          PRINT_REG(r_temp), PRINT_REG(s->r_acc));
						offset = 0;
					} else {
						r_ptr = s->r_acc;
						offset = r_temp.offset;
					}
				} else {
					r_ptr = r_temp;
					offset = s->r_acc.offset;
				}

				s->r_acc = pointer_add(s, r_ptr, offset);

			} else
				s->r_acc = make_reg(0, r_temp.offset + s->r_acc.offset);
			break;

		case op_sub: // 0x02 (02)
			r_temp = POP32();
			if (r_temp.segment != s->r_acc.segment) {
				reg_t r_ptr = NULL_REG;
				int offset;
				// Pointer arithmetics!
				if (s->r_acc.segment) {
					if (r_temp.segment) {
						error("Attempt to subtract two pointers, stack=%04x:%04x and acc=%04x:%04x",
						          PRINT_REG(r_temp), PRINT_REG(s->r_acc));
						offset = 0;
					} else {
						r_ptr = s->r_acc;
						offset = r_temp.offset;
					}
				} else {
					r_ptr = r_temp;
					offset = s->r_acc.offset;
				}

				s->r_acc = pointer_add(s, r_ptr, -offset);

			} else {
				// We can subtract numbers, or pointers with the same segment,
				// an operation which will yield a number like in C
				s->r_acc = make_reg(0, r_temp.offset - s->r_acc.offset);
			}
			break;

		case op_mul: // 0x03 (03)
			s->r_acc = ACC_ARITHMETIC_L(((int16)POP()) * (int16)/*acc*/);
			break;

		case op_div: { // 0x04 (04)
			int16 divisor = signed_validate_arithmetic(s->r_acc);
			s->r_acc = make_reg(0, (divisor != 0 ? ((int16)POP()) / divisor : 0));
			break;
		}
		case op_mod: { // 0x05 (05)
			int16 modulo = signed_validate_arithmetic(s->r_acc);
			s->r_acc = make_reg(0, (modulo != 0 ? ((int16)POP()) % modulo : 0));
			break;
		}
		case op_shr: // 0x06 (06)
			// Shift right logical
			s->r_acc = ACC_ARITHMETIC_L(((uint16)POP()) >> /*acc*/);
			break;

		case op_shl: // 0x07 (07)
			// Shift left logical
			s->r_acc = ACC_ARITHMETIC_L(((uint16)POP()) << /*acc*/);
			break;

		case op_xor: // 0x08 (08)
			s->r_acc = ACC_ARITHMETIC_L(POP() ^ /*acc*/);
			break;

		case op_and: // 0x09 (09)
			s->r_acc = ACC_ARITHMETIC_L(POP() & /*acc*/);
			break;

		case op_or: // 0x0a (10)
			s->r_acc = ACC_ARITHMETIC_L(POP() | /*acc*/);
			break;

		case op_neg: // 0x0b (11)
			s->r_acc = ACC_ARITHMETIC_L(-/*acc*/);
			break;

		case op_not: // 0x0c (12)
			s->r_acc = make_reg(0, !(s->r_acc.offset || s->r_acc.segment));
			// Must allow pointers to be negated, as this is used for checking whether objects exist
			break;

		case op_eq_: // 0x0d (13)
			// ==
			s->r_prev = s->r_acc;
			r_temp = POP32();
			s->r_acc = make_reg(0, r_temp == s->r_acc);
			// Explicitly allow pointers to be compared
			break;

		case op_ne_: // 0x0e (14)
			// !=
			s->r_prev = s->r_acc;
			r_temp = POP32();
			s->r_acc = make_reg(0, r_temp != s->r_acc);
			// Explicitly allow pointers to be compared
			break;

		case op_gt_: // 0x0f (15)
			// >
			s->r_prev = s->r_acc;
			r_temp = POP32();
			if (r_temp.segment && s->r_acc.segment) {
				// Signed pointer comparison. We do unsigned comparison instead, as that is probably what was intended.
				if (r_temp.segment != s->r_acc.segment)
					warning("[VM] Comparing pointers in different segments (%04x:%04x vs. %04x:%04x)", PRINT_REG(r_temp), PRINT_REG(s->r_acc));
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset > s->r_acc.offset);
			} else if (r_temp.segment && !s->r_acc.segment) {
				if (s->r_acc.offset >= 1000)
					error("[VM] op_gt: comparsion between a pointer and number");
				// Pseudo-WORKAROUND: sierra allows any pointer <-> value comparsion
				// Happens in SQ1, room 28, when throwing the water at Orat
				s->r_acc = make_reg(0, 1);
			} else
				s->r_acc = ACC_ARITHMETIC_L(signed_validate_arithmetic(r_temp) > (int16)/*acc*/);
			break;

		case op_ge_: // 0x10 (16)
			// >=
			s->r_prev = s->r_acc;
			r_temp = POP32();
			if (r_temp.segment && s->r_acc.segment) {
				if (r_temp.segment != s->r_acc.segment)
					warning("[VM] Comparing pointers in different segments (%04x:%04x vs. %04x:%04x)", PRINT_REG(r_temp), PRINT_REG(s->r_acc));
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset >= s->r_acc.offset);
			} else
				s->r_acc = ACC_ARITHMETIC_L(signed_validate_arithmetic(r_temp) >= (int16)/*acc*/);
			break;

		case op_lt_: // 0x11 (17)
			// <
			s->r_prev = s->r_acc;
			r_temp = POP32();
			if (r_temp.segment && s->r_acc.segment) {
				if (r_temp.segment != s->r_acc.segment)
					warning("[VM] Comparing pointers in different segments (%04x:%04x vs. %04x:%04x)", PRINT_REG(r_temp), PRINT_REG(s->r_acc));
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset < s->r_acc.offset);
			} else if (r_temp.segment && !s->r_acc.segment) {
				if (s->r_acc.offset >= 1000)
					error("[VM] op_lt: comparsion between a pointer and number");
				// Pseudo-WORKAROUND: sierra allows any pointer <-> value comparsion
				// Happens in SQ1, room 58, when giving id-card to robot
				s->r_acc = make_reg(0, 1);
			} else
				s->r_acc = ACC_ARITHMETIC_L(signed_validate_arithmetic(r_temp) < (int16)/*acc*/);
			break;

		case op_le_: // 0x12 (18)
			// <=
			s->r_prev = s->r_acc;
			r_temp = POP32();
			if (r_temp.segment && s->r_acc.segment) {
				if (r_temp.segment != s->r_acc.segment)
					warning("[VM] Comparing pointers in different segments (%04x:%04x vs. %04x:%04x)", PRINT_REG(r_temp), PRINT_REG(s->r_acc));
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset <= s->r_acc.offset);
			} else
				s->r_acc = ACC_ARITHMETIC_L(signed_validate_arithmetic(r_temp) <= (int16)/*acc*/);
			break;

		case op_ugt_: // 0x13 (19)
			// > (unsigned)
			s->r_prev = s->r_acc;
			r_temp = POP32();

			// SCI0/SCI1 scripts use this to check whether a
			// parameter is a pointer or a far text
			// reference. It is used e.g. by the standard library
			// Print function to distinguish two ways of calling it:
			//
			// (Print "foo") // Pointer to a string
			// (Print 420 5) // Reference to the fifth message in text resource 420

			// It works because in those games, the maximum resource number is 999,
			// so any parameter value above that threshold must be a pointer.
			if (r_temp.segment && (s->r_acc == make_reg(0, 1000)))
				s->r_acc = make_reg(0, 1);
			else if (r_temp.segment && s->r_acc.segment)
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset > s->r_acc.offset);
			else
				s->r_acc = ACC_ARITHMETIC_L(validate_arithmetic(r_temp) > /*acc*/);
			break;

		case op_uge_: // 0x14 (20)
			// >= (unsigned)
			s->r_prev = s->r_acc;
			r_temp = POP32();

			// See above
			if (r_temp.segment && (s->r_acc == make_reg(0, 1000)))
				s->r_acc = make_reg(0, 1);
			else if (r_temp.segment && s->r_acc.segment)
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset >= s->r_acc.offset);
			else
				s->r_acc = ACC_ARITHMETIC_L(validate_arithmetic(r_temp) >= /*acc*/);
			break;

		case op_ult_: // 0x15 (21)
			// < (unsigned)
			s->r_prev = s->r_acc;
			r_temp = POP32();

			// See above
			if (r_temp.segment && (s->r_acc == make_reg(0, 1000)))
				s->r_acc = NULL_REG;
			else if (r_temp.segment && s->r_acc.segment)
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset < s->r_acc.offset);
			else
				s->r_acc = ACC_ARITHMETIC_L(validate_arithmetic(r_temp) < /*acc*/);
			break;

		case op_ule_: // 0x16 (22)
			// <= (unsigned)
			s->r_prev = s->r_acc;
			r_temp = POP32();

			// See above
			if (r_temp.segment && (s->r_acc == make_reg(0, 1000)))
				s->r_acc = NULL_REG;
			else if (r_temp.segment && s->r_acc.segment)
				s->r_acc = make_reg(0, (r_temp.segment == s->r_acc.segment) && r_temp.offset <= s->r_acc.offset);
			else
				s->r_acc = ACC_ARITHMETIC_L(validate_arithmetic(r_temp) <= /*acc*/);
			break;

		case op_bt: // 0x17 (23)
			// Branch relative if true
			if (s->r_acc.offset || s->r_acc.segment)
				s->xs->addr.pc.offset += opparams[0];
			break;

		case op_bnt: // 0x18 (24)
			// Branch relative if not true
			if (!(s->r_acc.offset || s->r_acc.segment))
				s->xs->addr.pc.offset += opparams[0];
			break;

		case op_jmp: // 0x19 (25)
			s->xs->addr.pc.offset += opparams[0];
			break;

		case op_ldi: // 0x1a (26)
			// Load data immediate
			s->r_acc = make_reg(0, opparams[0]);
			break;

		case op_push: // 0x1b (27)
			// Push to stack
			PUSH32(s->r_acc);
			break;

		case op_pushi: // 0x1c (28)
			// Push immediate
			PUSH(opparams[0]);
			break;

		case op_toss: // 0x1d (29)
			// TOS (Top Of Stack) subtract
			s->xs->sp--;
			break;

		case op_dup: // 0x1e (30)
			// Duplicate TOD (Top Of Stack) element
			r_temp = s->xs->sp[-1];
			PUSH32(r_temp);
			break;

		case op_link: // 0x1f (31)
			// We shouldn't initialize temp variables at all
			//  We put special segment 0xFFFF in there, so that uninitialized reads can get detected
			for (int i = 0; i < opparams[0]; i++)
				s->xs->sp[i] = make_reg(0xffff, 0);

			s->xs->sp += opparams[0];
			break;

		case op_call: { // 0x20 (32)
			// Call a script subroutine
			int argc = (opparams[1] >> 1) // Given as offset, but we need count
			           + 1 + s->restAdjust;
			StackPtr call_base = s->xs->sp - argc;
			s->xs->sp[1].offset += s->restAdjust;

			uint16 localCallOffset = s->xs->addr.pc.offset + opparams[0];

			xs_new = add_exec_stack_entry(s->_executionStack, make_reg(s->xs->addr.pc.segment,
											localCallOffset),
											s->xs->sp, s->xs->objp,
											(validate_arithmetic(*call_base)) + s->restAdjust,
											call_base, NULL_SELECTOR, -1, localCallOffset, s->xs->objp,
											s->_executionStack.size()-1, s->xs->local_segment);
			s->restAdjust = 0; // Used up the &rest adjustment
			s->xs->sp = call_base;

			s->_executionStackPosChanged = true;
			break;
		}

		case op_callk: { // 0x21 (33)
			// Call kernel function
			gcCountDown(s);

			s->xs->sp -= (opparams[1] >> 1) + 1;

			bool oldScriptHeader = (getSciVersion() == SCI_VERSION_0_EARLY);
			if (!oldScriptHeader)
				s->xs->sp -= s->restAdjust;

			int argc = validate_arithmetic(s->xs->sp[0]);

			if (!oldScriptHeader)
				argc += s->restAdjust;

			callKernelFunc(s, opparams[0], argc);

			if (!oldScriptHeader)
				s->restAdjust = 0;

			// Calculate xs again: The kernel function might
			// have spawned a new VM

			xs_new = &(s->_executionStack.back());
			s->_executionStackPosChanged = true;

			// If a game is being loaded, stop processing
			if (s->abortScriptProcessing != kAbortNone || g_engine->shouldQuit())
				return; // Stop processing

			break;
		}

		case op_callb: // 0x22 (34)
			// Call base script
			temp = ((opparams[1] >> 1) + s->restAdjust + 1);
			s_temp = s->xs->sp;
			s->xs->sp -= temp;

			s->xs->sp[0].offset += s->restAdjust;
			xs_new = execute_method(s, 0, opparams[0], s_temp, s->xs->objp,
									s->xs->sp[0].offset, s->xs->sp);
			s->restAdjust = 0; // Used up the &rest adjustment
			if (xs_new)    // in case of error, keep old stack
				s->_executionStackPosChanged = true;
			break;

		case op_calle: // 0x23 (35)
			// Call external script
			temp = ((opparams[2] >> 1) + s->restAdjust + 1);
			s_temp = s->xs->sp;
			s->xs->sp -= temp;

			s->xs->sp[0].offset += s->restAdjust;
			xs_new = execute_method(s, opparams[0], opparams[1], s_temp, s->xs->objp,
									s->xs->sp[0].offset, s->xs->sp);
			s->restAdjust = 0; // Used up the &rest adjustment

			if (xs_new)  // in case of error, keep old stack
				s->_executionStackPosChanged = true;
			break;

		case op_ret: // 0x24 (36)
			// Return from an execution loop started by call, calle, callb, send, self or super
			do {
				StackPtr old_sp2 = s->xs->sp;
				StackPtr old_fp = s->xs->fp;
				ExecStack *old_xs = &(s->_executionStack.back());

				if ((int)s->_executionStack.size() - 1 == s->executionStackBase) { // Have we reached the base?
					s->executionStackBase = old_executionStackBase; // Restore stack base

					s->_executionStack.pop_back();

					s->_executionStackPosChanged = true;
					return; // "Hard" return
				}

				if (old_xs->type == EXEC_STACK_TYPE_VARSELECTOR) {
					// varselector access?
					reg_t *var = old_xs->getVarPointer(s->_segMan);
					if (old_xs->argc) // write?
						*var = old_xs->variables_argp[1];
					else // No, read
						s->r_acc = *var;
				}

				// Not reached the base, so let's do a soft return
				s->_executionStack.pop_back();
				s->_executionStackPosChanged = true;
				s->xs = &(s->_executionStack.back());

				if (s->xs->sp == CALL_SP_CARRY // Used in sends to 'carry' the stack pointer
				        || s->xs->type != EXEC_STACK_TYPE_CALL) {
					s->xs->sp = old_sp2;
					s->xs->fp = old_fp;
				}

			} while (s->xs->type == EXEC_STACK_TYPE_VARSELECTOR);
			// Iterate over all varselector accesses
			s->_executionStackPosChanged = true;
			xs_new = s->xs;

			break;

		case op_send: // 0x25 (37)
			// Send for one or more selectors
			s_temp = s->xs->sp;
			s->xs->sp -= ((opparams[0] >> 1) + s->restAdjust); // Adjust stack

			s->xs->sp[1].offset += s->restAdjust;
			xs_new = send_selector(s, s->r_acc, s->r_acc, s_temp,
									(int)(opparams[0] >> 1) + (uint16)s->restAdjust, s->xs->sp);

			if (xs_new && xs_new != s->xs)
				s->_executionStackPosChanged = true;

			s->restAdjust = 0;

			break;

		case 0x26: // (38)
		case 0x27: // (39)
			error("Dummy opcode 0x%x called", opcode);	// should never happen
			break;

		case op_class: // 0x28 (40)
			// Get class address
			s->r_acc = s->_segMan->getClassAddress((unsigned)opparams[0], SCRIPT_GET_LOCK,
											s->xs->addr.pc);
			break;

		case 0x29: // (41)
			error("Dummy opcode 0x%x called", opcode);	// should never happen
			break;

		case op_self: // 0x2a (42)
			// Send to self
			s_temp = s->xs->sp;
			s->xs->sp -= ((opparams[0] >> 1) + s->restAdjust); // Adjust stack

			s->xs->sp[1].offset += s->restAdjust;
			xs_new = send_selector(s, s->xs->objp, s->xs->objp,
									s_temp, (int)(opparams[0] >> 1) + (uint16)s->restAdjust,
									s->xs->sp);

			if (xs_new && xs_new != s->xs)
				s->_executionStackPosChanged = true;

			s->restAdjust = 0;
			break;

		case op_super: // 0x2b (43)
			// Send to any class
			r_temp = s->_segMan->getClassAddress(opparams[0], SCRIPT_GET_LOAD, s->xs->addr.pc);

			if (!r_temp.segment)
				error("[VM]: Invalid superclass in object");
			else {
				s_temp = s->xs->sp;
				s->xs->sp -= ((opparams[1] >> 1) + s->restAdjust); // Adjust stack

				s->xs->sp[1].offset += s->restAdjust;
				xs_new = send_selector(s, r_temp, s->xs->objp, s_temp,
										(int)(opparams[1] >> 1) + (uint16)s->restAdjust,
										s->xs->sp);

				if (xs_new && xs_new != s->xs)
					s->_executionStackPosChanged = true;

				s->restAdjust = 0;
			}

			break;

		case op_rest: // 0x2c (44)
			// Pushes all or part of the parameter variable list on the stack
			temp = (uint16) opparams[0]; // First argument
			s->restAdjust = MAX<int16>(s->xs->argc - temp + 1, 0); // +1 because temp counts the paramcount while argc doesn't

			for (; temp <= s->xs->argc; temp++)
				PUSH32(s->xs->variables_argp[temp]);

			break;

		case op_lea: // 0x2d (45)
			// Load Effective Address
			temp = (uint16) opparams[0] >> 1;
			var_number = temp & 0x03; // Get variable type

			// Get variable block offset
			r_temp.segment = s->variablesSegment[var_number];
			r_temp.offset = s->variables[var_number] - s->variablesBase[var_number];

			if (temp & 0x08)  // Add accumulator offset if requested
				r_temp.offset += signed_validate_arithmetic(s->r_acc);

			r_temp.offset += opparams[1];  // Add index
			r_temp.offset *= 2; // variables are 16 bit
			// That's the immediate address now
			s->r_acc = r_temp;
			break;


		case op_selfID: // 0x2e (46)
			// Get 'self' identity
			s->r_acc = s->xs->objp;
			break;

		case 0x2f: // (47)
			error("Dummy opcode 0x%x called", opcode);	// should never happen
			break;

		case op_pprev: // 0x30 (48)
			// Pushes the value of the prev register, set by the last comparison
			// bytecode (eq?, lt?, etc.), on the stack
			PUSH32(s->r_prev);
			break;

		case op_pToa: // 0x31 (49)
			// Property To Accumulator
			s->r_acc = validate_property(obj, (opparams[0] >> 1));
			break;

		case op_aTop: // 0x32 (50)
			// Accumulator To Property
			validate_property(obj, (opparams[0] >> 1)) = s->r_acc;
			break;

		case op_pTos: // 0x33 (51)
			// Property To Stack
			PUSH32(validate_property(obj, opparams[0] >> 1));
			break;

		case op_sTop: // 0x34 (52)
			// Stack To Property
			validate_property(obj, (opparams[0] >> 1)) = POP32();
			break;

		case op_ipToa: // 0x35 (53)
			// Incement Property and copy To Accumulator
			s->r_acc = validate_property(obj, (opparams[0] >> 1));
			s->r_acc = validate_property(obj, (opparams[0] >> 1)) = ACC_ARITHMETIC_L(1 + /*acc*/);
			break;

		case op_dpToa: { // 0x36 (54)
			// Decrement Property and copy To Accumulator
			s->r_acc = validate_property(obj, (opparams[0] >> 1));
			s->r_acc = validate_property(obj, (opparams[0] >> 1)) = ACC_ARITHMETIC_L(-1 + /*acc*/);
			break;
		}

		case op_ipTos: // 0x37 (55)
			// Increment Property and push to Stack
			validate_arithmetic(validate_property(obj, (opparams[0] >> 1)));
			temp = ++validate_property(obj, (opparams[0] >> 1)).offset;
			PUSH(temp);
			break;

		case op_dpTos: // 0x38 (56)
			// Decrement Property and push to Stack
			validate_arithmetic(validate_property(obj, (opparams[0] >> 1)));
			temp = --validate_property(obj, (opparams[0] >> 1)).offset;
			PUSH(temp);
			break;

		case op_lofsa: // 0x39 (57)
			// Load Offset to Accumulator
			s->r_acc.segment = s->xs->addr.pc.segment;

			switch (g_sci->_features->detectLofsType()) {
			case SCI_VERSION_1_1:
				s->r_acc.offset = opparams[0] + local_script->getScriptSize();
				break;
			case SCI_VERSION_1_MIDDLE:
				s->r_acc.offset = opparams[0];
				break;
			default:
				s->r_acc.offset = s->xs->addr.pc.offset + opparams[0];
			}

			if (s->r_acc.offset >= scr->getBufSize()) {
				error("VM: lofsa operation overflowed: %04x:%04x beyond end"
				          " of script (at %04x)\n", PRINT_REG(s->r_acc), scr->getBufSize());
			}
			break;

		case op_lofss: // 0x3a (58)
			// Load Offset to Stack
			r_temp.segment = s->xs->addr.pc.segment;

			switch (g_sci->_features->detectLofsType()) {
			case SCI_VERSION_1_1:
				r_temp.offset = opparams[0] + local_script->getScriptSize();
				break;
			case SCI_VERSION_1_MIDDLE:
				r_temp.offset = opparams[0];
				break;
			default:
				r_temp.offset = s->xs->addr.pc.offset + opparams[0];
			}

			if (r_temp.offset >= scr->getBufSize()) {
				error("VM: lofss operation overflowed: %04x:%04x beyond end"
				          " of script (at %04x)", PRINT_REG(r_temp), scr->getBufSize());
			}
			PUSH32(r_temp);
			break;

		case op_push0: // 0x3b (59)
			PUSH(0);
			break;

		case op_push1: // 0x3c (60)
			PUSH(1);
			break;

		case op_push2: // 0x3d (61)
			PUSH(2);
			break;

		case op_pushSelf: // 0x3e (62)
			if (!(extOpcode & 1)) {
				PUSH32(s->xs->objp);
			} else {
				// Debug opcode op_file, skip null-terminated string (file name)
				const byte *code_buf = scr->getBuf();
				while (code_buf[s->xs->addr.pc.offset++]) ;
			}
			break;

		case op_line: // 0x3f (63)
			// Debug opcode (line number)
			break;

		case op_lag: // 0x40 (64)
		case op_lal: // 0x41 (65)
		case op_lat: // 0x42 (66)
		case op_lap: // 0x43 (67)
			// Load global, local, temp or param variable into the accumulator
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			s->r_acc = READ_VAR(var_type, var_number);
			break;

		case op_lsg: // 0x44 (68)
		case op_lsl: // 0x45 (69)
		case op_lst: // 0x46 (70)
		case op_lsp: // 0x47 (71)
			// Load global, local, temp or param variable into the stack
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			PUSH32(READ_VAR(var_type, var_number));
			break;

		case op_lagi: // 0x48 (72)
		case op_lali: // 0x49 (73)
		case op_lati: // 0x4a (74)
		case op_lapi: // 0x4b (75)
			// Load global, local, temp or param variable into the accumulator,
			// using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			s->r_acc = READ_VAR(var_type, var_number);
			break;

		case op_lsgi: // 0x4c (76)
		case op_lsli: // 0x4d (77)
		case op_lsti: // 0x4e (78)
		case op_lspi: // 0x4f (79)
			// Load global, local, temp or param variable into the stack,
			// using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			PUSH32(READ_VAR(var_type, var_number));
			break;

		case op_sag: // 0x50 (80)
		case op_sal: // 0x51 (81)
		case op_sat: // 0x52 (82)
		case op_sap: // 0x53 (83)
			// Save the accumulator into the global, local, temp or param variable
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_ssg: // 0x54 (84)
		case op_ssl: // 0x55 (85)
		case op_sst: // 0x56 (86)
		case op_ssp: // 0x57 (87)
			// Save the stack into the global, local, temp or param variable
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			WRITE_VAR(var_type, var_number, POP32());
			break;

		case op_sagi: // 0x58 (88)
		case op_sali: // 0x59 (89)
		case op_sati: // 0x5a (90)
		case op_sapi: // 0x5b (91)
			// Save the accumulator into the global, local, temp or param variable,
			// using the accumulator as an additional index

			// Special semantics because it wouldn't really make a whole lot
			// of sense otherwise, with acc being used for two things
			// simultaneously...
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			s->r_acc = POP32();
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_ssgi: // 0x5c (92)
		case op_ssli: // 0x5d (93)
		case op_ssti: // 0x5e (94)
		case op_sspi: // 0x5f (95)
			// Save the stack into the global, local, temp or param variable,
			// using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			WRITE_VAR(var_type, var_number, POP32());
			break;

		case op_plusag: // 0x60 (96)
		case op_plusal: // 0x61 (97)
		case op_plusat: // 0x62 (98)
		case op_plusap: // 0x63 (99)
			// Increment the global, local, temp or param variable and save it
			// to the accumulator
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				s->r_acc = pointer_add(s, r_temp, 1);
			} else
				s->r_acc = make_reg(0, r_temp.offset + 1);
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_plussg: // 0x64 (100)
		case op_plussl: // 0x65 (101)
		case op_plusst: // 0x66 (102)
		case op_plussp: // 0x67 (103)
			// Increment the global, local, temp or param variable and save it
			// to the stack
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				r_temp = pointer_add(s, r_temp, 1);
			} else
				r_temp = make_reg(0, r_temp.offset + 1);
			PUSH32(r_temp);
			WRITE_VAR(var_type, var_number, r_temp);
			break;

		case op_plusagi: // 0x68 (104)
		case op_plusali: // 0x69 (105)
		case op_plusati: // 0x6a (106)
		case op_plusapi: // 0x6b (107)
			// Increment the global, local, temp or param variable and save it
			// to the accumulator, using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				s->r_acc = pointer_add(s, r_temp, 1);
			} else
				s->r_acc = make_reg(0, r_temp.offset + 1);
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_plussgi: // 0x6c (108)
		case op_plussli: // 0x6d (109)
		case op_plussti: // 0x6e (110)
		case op_plusspi: // 0x6f (111)
			// Increment the global, local, temp or param variable and save it
			// to the stack, using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				r_temp = pointer_add(s, r_temp, 1);
			} else
				r_temp = make_reg(0, r_temp.offset + 1);
			PUSH32(r_temp);
			WRITE_VAR(var_type, var_number, r_temp);
			break;

		case op_minusag: // 0x70 (112)
		case op_minusal: // 0x71 (113)
		case op_minusat: // 0x72 (114)
		case op_minusap: // 0x73 (115)
			// Decrement the global, local, temp or param variable and save it
			// to the accumulator
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				s->r_acc = pointer_add(s, r_temp, -1);
			} else
				s->r_acc = make_reg(0, r_temp.offset - 1);
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_minussg: // 0x74 (116)
		case op_minussl: // 0x75 (117)
		case op_minusst: // 0x76 (118)
		case op_minussp: // 0x77 (119)
			// Decrement the global, local, temp or param variable and save it
			// to the stack
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0];
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				r_temp = pointer_add(s, r_temp, -1);
			} else
				r_temp = make_reg(0, r_temp.offset - 1);
			PUSH32(r_temp);
			WRITE_VAR(var_type, var_number, r_temp);
			break;

		case op_minusagi: // 0x78 (120)
		case op_minusali: // 0x79 (121)
		case op_minusati: // 0x7a (122)
		case op_minusapi: // 0x7b (123)
			// Decrement the global, local, temp or param variable and save it
			// to the accumulator, using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				s->r_acc = pointer_add(s, r_temp, -1);
			} else
				s->r_acc = make_reg(0, r_temp.offset - 1);
			WRITE_VAR(var_type, var_number, s->r_acc);
			break;

		case op_minussgi: // 0x7c (124)
		case op_minussli: // 0x7d (125)
		case op_minussti: // 0x7e (126)
		case op_minusspi: // 0x7f (127)
			// Decrement the global, local, temp or param variable and save it
			// to the stack, using the accumulator as an additional index
			var_type = opcode & 0x3; // Gets the variable type: g, l, t or p
			var_number = opparams[0] + signed_validate_arithmetic(s->r_acc);
			r_temp = READ_VAR(var_type, var_number);
			if (r_temp.segment) {
				// Pointer arithmetics!
				r_temp = pointer_add(s, r_temp, -1);
			} else
				r_temp = make_reg(0, r_temp.offset - 1);
			PUSH32(r_temp);
			WRITE_VAR(var_type, var_number, r_temp);
			break;

		default:
			error("run_vm(): illegal opcode %x", opcode);

		} // switch (opcode)

		if (s->_executionStackPosChanged) // Force initialization
			s->xs = xs_new;

		if (s->xs != &(s->_executionStack.back())) {
			error("xs is stale (%p vs %p); last command was %02x",
					(void *)s->xs, (void *)&(s->_executionStack.back()),
					opcode);
		}
		++s->scriptStepCounter;
	}
}

reg_t *ObjVarRef::getPointer(SegManager *segMan) const {
	Object *o = segMan->getObject(obj);
	return o ? &o->getVariableRef(varindex) : 0;
}

reg_t *ExecStack::getVarPointer(SegManager *segMan) const {
	assert(type == EXEC_STACK_TYPE_VARSELECTOR);
	return addr.varp.getPointer(segMan);
}

} // End of namespace Sci
