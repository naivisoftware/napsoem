/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

 // Local Includes
#include "ethercatmaster.h"

// External Includes
#include <nap/logger.h>
#include <soem/ethercat.h>
#include <assert.h>

//  All available Ethercat slave states
RTTI_BEGIN_ENUM(nap::EtherCATMaster::ESlaveState)
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::None,				"None"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::Init,				"Init"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::PreOperational,	"PreOperational"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::Boot,				"Boot"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::SafeOperational,	"SafeOperational"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::Operational,		"Operational"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::Error,			"Error"),
	RTTI_ENUM_VALUE(nap::EtherCATMaster::ESlaveState::ACK,				"ACK")
RTTI_END_ENUM

// nap::ethercatmaster run time class definition
RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::EtherCATMaster)
	RTTI_PROPERTY("ForceOperational",		&nap::EtherCATMaster::mForceOperational,		nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("ForceSafeOperational",	&nap::EtherCATMaster::mForceSafeOperational,	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("ForcePreOperational",	&nap::EtherCATMaster::mForcePreOperational,		nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("OperationalTimeout",		&nap::EtherCATMaster::mOperationalTimeout,		nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("SafeOperationalTimeout",	&nap::EtherCATMaster::mSafeOperationalTimeout,	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("PreOperationalTimeout",	&nap::EtherCATMaster::mPreOperationalTimeout,	nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("Adapter",				&nap::EtherCATMaster::mAdapter,					nap::rtti::EPropertyMetaData::Required)
	RTTI_PROPERTY("ErrorCycleTime",			&nap::EtherCATMaster::mErrorCycleTime,			nap::rtti::EPropertyMetaData::Default)
	RTTI_PROPERTY("RecoveryTimeout",		&nap::EtherCATMaster::mRecoveryTimeout,			nap::rtti::EPropertyMetaData::Default)
RTTI_END_CLASS

//////////////////////////////////////////////////////////////////////////

namespace nap
{
	//////////////////////////////////////////////////////////////////////////
	// SOEM Context Wrapper
	//////////////////////////////////////////////////////////////////////////

	namespace soem
	{
		/**
		 * Data associated with a single SOEM context
		 */
		struct ContextData
		{
			ContextData()
			{
				// Initialize redundancy port to null, prevents error when closing socket
				ecx_port_fsoe.redport = nullptr;
			}

			char IOmap[4096];
			ec_slavet   		ec_slave[EC_MAXSLAVE]; 		///< number of slaves found on the network
			int         		ec_slavecount;				///< Slave group structure
			ec_groupt   		ec_groups[EC_MAXGROUP];		///< cache for EEPROM read functions
			uint8        		esibuf[EC_MAXEEPBUF];		///< bitmap for filled cache buffer bytes
			uint32       		esimap[EC_MAXEEPBITMAP];	///< current slave for EEPROM cache buffer
			ec_eringt    		ec_elist;					///< ringbuf for error storage
			ec_idxstackT 		ec_idxstack;				///< SyncManager Communication Type struct to store data of one slave
			ec_SMcommtypet  	ec_SMcommtype;				///< PDO assign struct to store data of one slave
			ec_PDOassignt   	ec_PDOassign;				///< PDO description struct to store data of one slave
			ec_PDOdesct     	ec_PDOdesc;					///< buffer for EEPROM SM data
			ec_eepromSMt 		ec_SM;						///< buffer for EEPROM FMMU data
			ec_eepromFMMUt 		ec_FMMU;					///< Global variable TRUE if error available in error stack
			boolean    			AppEcatError = FALSE;		///< SII FMMU structure
			int64         		ec_DCtime;					///< Distributed clock time
			ecx_portt    		ecx_port_fsoe;				///< pointer structure to buffers, vars and mutexes for port instantiation
		};

		/**
		 * Wrapped SOEM context.
		 */
		struct Context
		{
			// Create SOEM context on construction
			Context(EtherCATMaster& master)
			{
				// Create context
				mContext = new ecx_contextt
				{
					&mData.ecx_port_fsoe,
					&mData.ec_slave[0],
					&mData.ec_slavecount,
					EC_MAXSLAVE,
					&mData.ec_groups[0],
					EC_MAXGROUP,
					&mData.esibuf[0],
					&mData.esimap[0],
					0,
					&mData.ec_elist,
					&mData.ec_idxstack,
					&mData.AppEcatError,
					&mData.ec_DCtime,
					&mData.ec_SMcommtype,
					&mData.ec_PDOassign,
					&mData.ec_PDOdesc,
					&mData.ec_SM,
					&mData.ec_FMMU,
					NULL,
					NULL,
					0,
					&master
				};
			}

			// Delete SOEM context
			~Context()
			{
				delete mContext;
				mContext = nullptr;
			}

			ecx_contextt* mContext = nullptr;		///< Soem (ethercat master) context
			nap::soem::ContextData 	mData;			///< Data associated with context
		};
	}

	static ecx_contextt* toContext(void* ctx)
	{
		return reinterpret_cast<soem::Context*>(ctx)->mContext;
	}


	//////////////////////////////////////////////////////////////////////////
	// EtherCATMaster
	//////////////////////////////////////////////////////////////////////////


	EtherCATMaster::~EtherCATMaster()
	{
		// Destroy SOEM context
		delete reinterpret_cast<soem::Context*>(mContext);
		mContext = nullptr;
	}


	EtherCATMaster::EtherCATMaster()
	{
		// Create context
		mContext = new soem::Context(*this);
	}


	bool EtherCATMaster::start(utility::ErrorState& errorState)
	{
		// No tasks should be active at this point
		// The master shouldn't be in operation
		assert(!mOperational);
		assert(!mStarted);


		//////////////////////////////////////////////////////////////////////////
		// Init
		//////////////////////////////////////////////////////////////////////////

		// Try to initialize adapter
		ecx_contextt* context = toContext(mContext);

		// Acquire names of available adapters
        std::vector<std::string> adapters;
        {
            ec_adaptert *adapter = ec_find_adapters();
            while (adapter != NULL)
            {
                adapters.emplace_back(adapter->name);
                adapter = adapter->next;
            }
        }

        // Check if the configured adapter name is among the available adapters
        auto it = std::find_if(adapters.begin(), adapters.end(), [adapter = mAdapter](const auto& it)
        {
            return it == adapter.c_str();
        });

	    // Ensure the configured adapter name is among the available adapters, else fail
        if (!errorState.check(it != adapters.end(), "%s: configured adapter %s does not exist", mID.c_str(), mAdapter.c_str()))
            return false;

        // Initialize the adapter
		if (!ecx_init(context, mAdapter.c_str()))
		{
			errorState.fail("%s: no socket connection: %s", mID.c_str(), mAdapter.c_str());
			return false;
		}

		// Initialize all slaves, lib initialization is allowed to fail
		// Simply means no slaves are available on the network
		if (ecx_config_init(context, false) <= 0)
		{
			nap::Logger::warn("%s: no slaves found", mID.c_str());
			mStarted = true;
			return true;
		}

		// Slaves in init state
		nap::Logger::info("%d slaves found and configured", *(toContext(mContext)->slavecount));

		// Configure DC options for every DC capable slave found in the list
		ecx_configdc(context);


		//////////////////////////////////////////////////////////////////////////
		// Pre Operational
		//////////////////////////////////////////////////////////////////////////

		// All slaves should be in pre-op mode now, if not show and bail if required
		if (stateCheck(0, ESlaveState::PreOperational, mPreOperationalTimeout) != ESlaveState::PreOperational)
		{
			nap::Logger::warn("%s: not all slaves reached pre-operational state", mID.c_str());

			utility::ErrorState pre_op_error;
			getStatusMessage(ESlaveState::PreOperational, pre_op_error);
			if (mForcePreOperational)
			{
				errorState = pre_op_error;
				stop();
				return false;
			}
			nap::Logger::warn(pre_op_error.toString());
		}
		else
		{
			nap::Logger::info("%s: all slaves reached pre-operational state", mID.c_str());
		}

		// Allow derived class to startup, bail on failure
		readState();
		if (!onStarted(errorState))
		{
			stop();
			return false;
		}

		// Notify listeners, first update individual slave states
		// Bail if pre-operation callback of any of the slaves failed
		bool pre_op_success = true;
		for (int i = 1; i <= getSlaveCount(); i++)
		{
			ec_slavet& cs = context->slavelist[i];
			if (cs.state == static_cast<uint16>(ESlaveState::PreOperational))
			{
				if (!onPreOperational(&cs, i, errorState))
				{
					pre_op_success = false;
				}
			}
		}
		if (!pre_op_success)
		{
			stop();
			return false;
		}

		nap::Logger::info("%s: all slaves advanced pre-operational hooks", mID.c_str());
		

		//////////////////////////////////////////////////////////////////////////
		// Safe Operational
		//////////////////////////////////////////////////////////////////////////

		// Map all PDOs from slaves to IOmap with Outputs/Inputs
		nap::Logger::info("%s: mapping slaves...", mID.c_str());
		ecx_config_map_group(context, &mIOmap, 0);

		nap::Logger::info("%s: all slaves mapped", mID.c_str());

		// All slaves should be in safe-op mode 
		if (stateCheck(0, ESlaveState::SafeOperational, mSafeOperationalTimeout) != ESlaveState::SafeOperational)
		{
			nap::Logger::warn("%s: not all slaves reached safe-operational state", mID.c_str());

			utility::ErrorState safe_op_error;
			getStatusMessage(ESlaveState::SafeOperational, safe_op_error);
			if (mForceSafeOperational)
			{
				errorState = safe_op_error;
				stop();
				return false;
			}
			nap::Logger::warn(safe_op_error.toString());
		}
		else
		{
			nap::Logger::info("%s: all slaves reached safe-operational state", mID.c_str());
		}

		// Calculate slave work counter, used to check in the error loop if slaves got lost
		ec_groupt& master_group = context->grouplist[0];
		mExpectedWKC = (master_group.outputsWKC * 2) + master_group.inputsWKC;
		nap::Logger::info("%s: calculated work-counter: %d", mID.c_str(), mExpectedWKC);

		// Print useful info and notify listeners
		// Bail if safe operational callback failed, unsafe to proceed
		readState();
		bool safe_op_success = true;
		for (int i = 1; i <= getSlaveCount(); i++)
		{
			ec_slavet& cs = context->slavelist[i];

			ESlaveState state = static_cast<ESlaveState>(cs.state);
			std::string state_label = RTTI_OF(ESlaveState).get_enumeration().value_to_name(state).to_string();
			nap::Logger::info("Slave:%d Name:%s Output size:%3dbits Input size:%3dbits State:%2d delay:%d.%d",
				i, cs.name, cs.Obits, cs.Ibits, state_label.c_str(), (int)cs.pdelay, cs.hasdc);

			if (context->slavelist[i].state == static_cast<uint16>(ESlaveState::SafeOperational))
			{
				safe_op_success = onSafeOperational(&context->slavelist[i], i, errorState);
			}
		}
		if (!safe_op_success)
		{
			errorState.fail("%s: failed to complete safe-operational setup", mID.c_str());
			stop();
			return false;
		}
		nap::Logger::info("%s: all slaves advanced safe-operational hooks", mID.c_str());

		//////////////////////////////////////////////////////////////////////////
		// Operational
		//////////////////////////////////////////////////////////////////////////

		// Enable run mode, we're operational when all slaves reach operational state
		mOperational = false;

		// Bind our thread and start sending / receiving slave data
		// We do that here to ensure a fast transition from safe to operational.
		mRunning = true;
		mProcessTask = std::async(std::launch::async, std::bind(&EtherCATMaster::process, this));

		// Bind our error thread, run until stopped
		// We do that here to ensure a fast transition from safe to operational.
		mStopErrorTask = false;
		mErrorTask = std::async(std::launch::async, std::bind(&EtherCATMaster::checkForErrors, this));

		// request Operational state for all slaves, give it at least mOperationalTimeout ms
		nap::Logger::info("Request operational state for all slaves");
		ESlaveState state = requestState(ESlaveState::Operational, mOperationalTimeout);

		// If not all slaves reached operational state display errors and
		// bail if operational state of all slaves is required on startup
		if (state != EtherCATMaster::ESlaveState::Operational)
		{
			nap::Logger::info("%s: not all slaves reached operational state in %dms", mID.c_str(), mOperationalTimeout);

			// Get error message
			utility::ErrorState op_error;
			getStatusMessage(ESlaveState::Operational, op_error);
			nap::Logger::warn(op_error.toString());

			// Stop if operational state for all slaves is required
			if (mForceOperational)
			{
				stop();
				errorState.fail(utility::stringFormat("%s: not all slaves reached operational state (%dms timeout exceeded)!", mID.c_str(), mOperationalTimeout));
				return false;
			}
		}
		else
		{
			nap::Logger::info("%s: all slaves reached operational state", mID.c_str());
		}

		// Master is operational, starts error handling thread
		mOperational = true;
		mStarted = true;
		return true;
	}


	void EtherCATMaster::stop()
	{
		// When operational, tasks are running and at least 1 slave is found.
		// This means we can stop the tasks and request the slave to go to init state.
		if (mRunning)
		{
			// Stop error reporting task
			assert(mErrorTask.valid());
			mStopErrorTask = true;
			mErrorTask.wait();

			// Stop processing task
			assert(mProcessTask.valid());
			onStopProcessing();
			mProcessTask.wait();

			// Make all slaves go to initialization stage
			if (requestState(ESlaveState::Init) != ESlaveState::Init)
			{
				nap::Logger::warn("%s: not all slaves reached init state", mID.c_str());
			}
		}

		// Close socket
		if (mStarted || mRunning)
		{
			onStopped();
			ecx_contextt* context = toContext(mContext);
			ecx_close(context);
		}

		// Reset work-counter and other variables
		mActualWCK = 0;
		mExpectedWKC = 0;
		mRunning = false;
		mOperational = false;
		mStarted = false;
	}


	bool EtherCATMaster::isRunning() const
	{
		return mRunning;
	}


	bool EtherCATMaster::started() const
	{
		return mStarted;
	}


	bool EtherCATMaster::isOnline(int index) const
	{
		return !isLost(index);
	}


	int EtherCATMaster::getSlaveCount() const
	{
		return *toContext(mContext)->slavecount;
	}


	nap::EtherCATMaster::ESlaveState EtherCATMaster::getSlaveState(int index) const
	{
		return getState(index);
	}


	nap::EtherCATMaster::ESlaveState EtherCATMaster::readState()
	{
		return static_cast<ESlaveState>(ecx_readstate(toContext(mContext)));
	}


	nap::EtherCATMaster::ESlaveState EtherCATMaster::getState(int index) const
	{
		ecx_contextt* context = toContext(mContext);
		assert(index <= *context->slavecount);
		uint16 cstate = context->slavelist[index].state;
		return cstate > static_cast<uint16>(EtherCATMaster::ESlaveState::Operational) ?
			EtherCATMaster::ESlaveState::Error :
			static_cast<EtherCATMaster::ESlaveState>(cstate);
	}


	bool EtherCATMaster::isLost(int index) const
	{
		assert(index <= getSlaveCount());
		return toContext(mContext)->slavelist[index].islost > 0;
	}


	void* EtherCATMaster::getSlave(int index)
	{
		assert(index <= getSlaveCount());
		return &(toContext(mContext)->slavelist[index]);
	}


	void EtherCATMaster::sdoWrite(uint16 slave, uint16 index, uint8 subindex, bool ca, int psize, void* p)
	{
		ecx_SDOwrite(toContext(mContext), slave, index, subindex, ca, psize, p, EC_TIMEOUTRXM);
	}


	int EtherCATMaster::sdoRead(uint16 slave, uint16 index, uint8 subindex, bool ca, int* psize, void* p)
	{
		return ecx_SDOread(toContext(mContext), slave, index, subindex, ca, psize, p, EC_TIMEOUTRXM);
	}


	void EtherCATMaster::process()
	{
		onProcess();
	}


	void EtherCATMaster::checkForErrors()
	{
		while (!mStopErrorTask)
		{
			// Operational stage not yet reached and no issues (group 0)
			ec_groupt& cg = toContext(mContext)->grouplist[0];
			if (!mOperational || (mActualWCK == mExpectedWKC && !(cg.docheckstate)))
			{
				osal_usleep(mErrorCycleTime);
				continue;
			}

			// One or more slaves are not responding
			processErrors();
			osal_usleep(mErrorCycleTime);
		}
		nap::Logger::info("%s: error process stopped", mID.c_str());
	}


	void EtherCATMaster::processErrors()
	{
		bool was_operational = mOperational;

		ecx_contextt* ctx = toContext(mContext);
		ec_groupt& cg = ctx->grouplist[0];
		cg.docheckstate = false;
		ecx_readstate(ctx);

		for (int slave = 1; slave <= *ctx->slavecount; slave++)
		{
			ec_slavet& cs = ctx->slavelist[slave];
			if ((cs.group == 0) && (cs.state != static_cast<uint16>(ESlaveState::Operational)))
			{
				// Signal slave error in group
				cg.docheckstate = true;

				// In safe state with error bit set
				if (cs.state == static_cast<uint16>(EtherCATMaster::ESlaveState::SafeOperational) +
					static_cast<uint16>(EtherCATMaster::ESlaveState::Error))
				{
					nap::Logger::error("%s: slave %d is in SAFE_OP + ERROR, attempting ack", mID.c_str(), slave);
					cs.state = (
						static_cast<uint16>(EtherCATMaster::ESlaveState::SafeOperational) +
						static_cast<uint16>(EtherCATMaster::ESlaveState::ACK));
					writeState(slave);
				}

				// Safe operational
				else if (cs.state == static_cast<uint16>(EtherCATMaster::ESlaveState::SafeOperational))
				{
					nap::Logger::warn("%s: slave %d is in SAFE_OP, change to OPERATIONAL", mID.c_str(), slave);
					cs.state = static_cast<uint16>(EtherCATMaster::ESlaveState::Operational);
					writeState(slave);
				}

				// Slave in between none and safe operational
				else if (cs.state > static_cast<uint16>(EtherCATMaster::ESlaveState::None))
				{
					if (ecx_reconfig_slave(ctx, slave, mRecoveryTimeout))
					{
						cs.islost = false;
						nap::Logger::info("%s: slave %d reconfigured", mID.c_str(), slave);
					}
				}

				// Slave might have gone missing
				else if (!isLost(slave))
				{
					// Check if the slave is operational
					// If slave appears to be missing (none) tag it.
					stateCheck(slave, ESlaveState::Operational, EC_TIMEOUTSTATE / 1000);
					if (cs.state == static_cast<uint16>(ESlaveState::None))
					{
						cs.islost = true;
						nap::Logger::info("%s: slave %d lost", mID.c_str(), slave);
					}
				}
			}

			// Slave appears to be lost,
			if (isLost(slave))
			{
				if (cs.state == static_cast<uint16>(ESlaveState::None))
				{
					if (ecx_recover_slave(ctx, slave, mRecoveryTimeout))
					{
						cs.islost = false;
						nap::Logger::info("%s: slave %d recovered", mID.c_str(), slave);
					}
				}
				else
				{
					cs.islost = false;
					nap::Logger::info("%s: slave %d found", mID.c_str(), slave);
				}
			}

			// Exit early if the error task is stopped
			if (mStopErrorTask)
				return;
		}

		// Check current group state, notify when all slaves resumed operational state
		if (!was_operational && !(cg.docheckstate))
			nap::Logger::info("%s: all slaves resumed OPERATIONAL", mID.c_str());
	}


	void EtherCATMaster::getStatusMessage(ESlaveState requiredState, utility::ErrorState& outLog)
	{
		readState();
		ecx_contextt* ctx = toContext(mContext);
		for (int i = 1; i <= *ctx->slavecount; i++)
		{
			ec_slavet& cs = ctx->slavelist[i];
			if (cs.state == static_cast<uint16>(requiredState))
				continue;

			std::string fail_state = utility::stringFormat("Slave %d State=0x%2.2x StatusCode=0x%4.4x : %s",
				i, cs.state, cs.ALstatuscode, ec_ALstatuscode2string(cs.ALstatuscode));
			outLog.fail(fail_state);
		}
	}


	EtherCATMaster::ESlaveState EtherCATMaster::requestState(ESlaveState state, int timeout)
	{
		// Force state for all slaves
		ec_slavet& ms = toContext(mContext)->slavelist[0];
		ms.state = static_cast<uint16>(state);
		writeState(0);
		stateCheck(0, state, timeout);
		return static_cast<EtherCATMaster::ESlaveState>(ms.state);
	}


	void EtherCATMaster::writeState(int index)
	{
		ecx_writestate(toContext(mContext), index);
	}


	EtherCATMaster::ESlaveState EtherCATMaster::stateCheck(int index, ESlaveState state, int timeout)
	{
		return static_cast<EtherCATMaster::ESlaveState>(
			ecx_statecheck(toContext(mContext), index, static_cast<uint16>(state), timeout * 1000));
	}


	int EtherCATMaster::receiveProcessData(int timeout)
	{
		mActualWCK = ecx_receive_processdata(toContext(mContext), timeout);
		return mActualWCK == mExpectedWKC;
	}


	void EtherCATMaster::sendProcessData()
	{
		ecx_send_processdata(toContext(mContext));
	}


	bool EtherCATMaster::hasDistributedClock(int slave)
	{
		return toContext(mContext)->slavelist[slave].hasdc;
	}


	bool EtherCATMaster::hasDistributedClock()
	{
		return toContext(mContext)->slavelist[0].hasdc;
	}


	int64 EtherCATMaster::getDistributedClock()
	{
		return *toContext(mContext)->DCtime;
	}


	int64 EtherCATMaster::syncClock(uint32 cycleTime, int32 dcOffset, int64& outCompensation, int64& ioIntegral)
	{
		int64 delta;
		int32 dcoffset_ns = dcOffset * 1000;		// Master offset ns
		int32 cyletime_ns = cycleTime * 1000;		// Frame cycle time ns

		assert(hasDistributedClock());
		delta = (getDistributedClock() - dcoffset_ns) % cyletime_ns;
		if (delta > (cyletime_ns / 2)) { delta = delta - cyletime_ns; }
		if (delta > 0) { ioIntegral++; }
		if (delta < 0) { ioIntegral--; }
		outCompensation = -(delta / 100) - (ioIntegral / 20);
		return delta;
	}


	std::string EtherCATMaster::getSlaveName(int index)
	{
		assert(index <= getSlaveCount());
		return toContext(mContext)->slavelist[index].name;
	}


	void* EtherCATMaster::getContext()
	{
		return toContext(mContext);
	}
}
