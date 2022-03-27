#pragma once

#include <cstdint>

#include "modm/platform.hpp"

using namespace modm::literals;
using namespace modm::platform;
struct SystemClock
{
	// Chosen to achieve 100MHz system clock
	static constexpr uint32_t PllAMult = 3051;
	// Chosen to achieve 48MHz USB clock
	static constexpr uint32_t PllBMult = 1465;

	static constexpr uint32_t Frequency = PllAMult * SlowClkFreqHz;
	static constexpr uint32_t Usb = PllBMult * SlowClkFreqHz;
	static constexpr uint32_t Mck = Frequency;
	static constexpr uint32_t Pck0 = Mck;
	static constexpr uint32_t Pck1 = Mck;
	static constexpr uint32_t Pck2 = Mck;
	static constexpr uint32_t Pck3 = Mck;
	static constexpr uint32_t Pck4 = Mck;
	static constexpr uint32_t Pck5 = Mck;
	static constexpr uint32_t Pck6 = Mck;
	static constexpr uint32_t Pck7 = Mck;

	static bool inline
	enable()
	{
		ClockGen::setFlashLatency<Frequency>();
		ClockGen::updateCoreFrequency<Frequency>();
		ClockGen::enableExternal32Khz(true);
		ClockGen::enablePllA<PllAMult>();
		ClockGen::selectMasterClk<MasterClkSource::PLLA_CLK, MasterClkPrescaler::CLK_1>();
		return true;
	}

	static bool inline
	enableUsb()
	{
		ClockGen::enablePllB<PllBMult>();
		// Use PLLB as USB clock source
		PMC->PMC_USB = PMC_USB_USBS;
		return true;
	}
};