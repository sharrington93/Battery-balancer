/*
 * BatteryController.c
 *
 *  Created on: Mar 4, 2016
 *      Author: Sean Harrington
 */

#include "BatteryController.h"
#include "State.h"
#include "CellStatus.h"
#include "Timer.h"
#include "SPI.h"
#include "GPIO.h"
#include "I2C_Coms.h"
#include "Initialize.h"

//-----------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------

extern cell_t cells[CELLS_IN_SERIES];

//-----------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------

/// Hysteresis voltage (in millivolts) for bulk charge on/off. No cell
/// can be higher than CHARGE_ON_MAX_VOLT - CHARGE_ON_VOLT_TOL in order
/// for bulk charging to be enabled.
///
/// To avoid oscillation, this parameter should be set greater than
/// approx. (Ibulk) * Ri, where Ibulk is the DC charge current setpoint and
/// Ri is the typical internal resistance of a series cell at high SOC.
#define CHARGE_ON_VOLT_TOL			(50) 	//mV

/// The minimum voltage (in millivolts) that a cell should have in order
/// to be eligible for balancing.
#define BALANCE_MIN_VOLT_THRESH		(3000) 	//mV

///	Voltage difference threshold required to enable balancing for a cell.
/// If a cell is not currently balancing, it must be more than
/// BALANCE_ON_VOLT_TOL volts above the lowest cell in order to begin
///being balanced.
#define BALANCE_ON_VOLT_TOL			(25) 	//mV

/// Voltage difference threshold required to disable balancing for a cell.
/// If a cell is currently balancing, it must be within
/// BALANCE_OFF_VOLT_TOL volts of the minimum cell in order to stop being
/// balanced.
///
/// If this value is negative, a balancing cell will be balanced until it
/// has a lower voltage (under load) than the minimum non-balancing cell.
#define BALANCE_OFF_VOLT_TOL		(-50)	//mV

/// Time that an individual cell must relax after balancing before it
/// can be balanced again.
#define BALANCE_RELAXATION_TIME		(5000)	// mSeconds

//-----------------------------------------------------------------------
// Private variables
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
// Private (Internal) functions
//-----------------------------------------------------------------------

Bool batteryController_NeedsBalanced(cell_t * cell);

void batteryController_TimerCallback(void * timerAddr);

//-----------------------------------------------------------------------
// Public functions
//-----------------------------------------------------------------------

void BatteryController_Task(void)
{
    SoftwareInit();

	uint16_t i;

	timer_t balanceDoneTimer;
	Timer_Setup(&balanceDoneTimer, NULL);

	// Run this task forever.
	// todo: Update this to variable that disables on error
	while(1)
	{
		I2C_Update();
		state_t state = GetState();

		if ((state == CHARGE) || (state == CHARGE_BALANCE))
		{
			for (i = 0; i < CELLS_IN_SERIES; i++)
			{
				// If any cell above maximum cell voltage
				if (cells[i].voltage >= MAX_CELL_VOLTAGE)
				{
					// If only charging, go to wait. If charge and balance,
					// go to balance.
					(state == CHARGE) ? SetState(WAIT) : SetState(BALANCE);
				}
				if (cells[i].voltage >= MAX_CELL_CRITICAL_VOLTAGE)
				{
					// Go into error state.
					// todo: Assert error
					SetState(ERROR);
				}
			}
		}
		if ((state == BALANCE) || (state == CHARGE_BALANCE))
		{
			// todo: This probably needs changed. We will leave balancing
			// as soon as no cells are balancing but we should wait for
			// relaxation time before knowing for sure to leave this state
			Bool doneBalancing = TRUE;

			for (i = 0; i < CELLS_IN_SERIES; i++)
			{
				if (batteryController_NeedsBalanced(&cells[i]))
				{
					cells[i].balance = TRUE;
					doneBalancing = FALSE;
				}
				else
				{
					cells[i].balance = FALSE;
				}
			}
			if (doneBalancing)
			{
				/// No cells balancing. Start timer that will transition
				/// states if cell doesn't balance in #BALANCE_RELAXATION_TIME
				if (!Timer_IsActive(&balanceDoneTimer))
				{
					Timer_Start(&balanceDoneTimer, BALANCE_RELAXATION_TIME);
				}
			}
			else
			{
				/// Cell still balancing. Stop balancer timeout timer.
				Timer_Stop(&balanceDoneTimer);
			}
		}
		if (state == WAIT)
		{
			//TEMPORARY

			uint8_t expanderInputPort0 = I2C_GetPortInput(PORT_0);
			uint8_t expanderInputPort1 = I2C_GetPortInput(PORT_1);
		    uint16_t * testPtr = NULL;

			/// Update SPI outputs
			Uint16 i = 0;
			for (i = 0; i < DRV8860_IN_SERIES; i++)
			{
				/// Open all relays
				SPI_PushToQueue(0xFF, RELAYS);
			}
			SPI_SendTx(RELAYS);

			//SPI_DRV8860_GetFaults(testPtr, 1);

			if (expanderInputPort0 & START_BUTTON)
			{
				if (expanderInputPort0 & SWITCH_CHARGE_AND_BALANCE)
				{
					SetState(CHARGE_BALANCE);
				}
				else if (expanderInputPort0 & SWITCH_CHARGE)
				{
					// Set next state to CHARGE
					SetState(CHARGE);
				}
				else
				{
					// Balance only mode
					SetState(BALANCE);
				}
			}
		}
	}
}

Bool batteryController_NeedsBalanced(cell_t * cell)
{
	if (Timer_HasElapsed(&cell->relaxationTimer) &&
			(cell->balance == FALSE))
	{
		return ((cell->voltage - CellStatus_MinCellVolt()) >
				BALANCE_OFF_VOLT_TOL);
	}
	else
	{
		return FALSE;
	}
}

void batteryController_TimerCallback(void * timerAddr)
{
	/// Change state to WAIT if done balancing. Set to CHARGE if
	/// state was CHARGE_BALANCE
	(GetState() == BALANCE) ? SetState(WAIT) : SetState(CHARGE);
}
