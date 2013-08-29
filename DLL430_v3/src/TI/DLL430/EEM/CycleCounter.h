/*
 * CycleCounter.h
 *
 * Handles cycle counter functionality.
 *
 * Copyright (C) 2007 - 2011 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                                                                                                                                                                                                                                                                                         
 */

#if _MSC_VER > 1000
#pragma once
#endif
#ifndef DLL430_CYCLECOUNTER_H
#define DLL430_CYCLECOUNTER_H

#include <stdint.h>
#include "TemplateDeviceDb/MSP430Defaults.h"

namespace TI
{
	namespace DLL430
	{

		class DeviceInfo;
		class DeviceHandleV3;

		class CycleCounter
		{
		public:
			explicit CycleCounter(DeviceHandleV3* deviceHandle, const DeviceInfo* info);

			void reset();
			void configure();
			void countInstruction(uint32_t instruction, bool steppedIntoInterrupt);
			uint64_t read();
			uint64_t getValue() const { return counterValue; }

		private:
			DeviceHandleV3* deviceHandle;
			TemplateDeviceDb::EMEX_MODE emulationLevel;

			uint32_t cycleCounterControl;
			uint64_t counterValue;
			bool isCpuX;
		};

	}
};

#endif /* DLL430_CYCLECOUNTER_H */