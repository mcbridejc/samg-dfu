#include <modm/platform.hpp>
#include <tinyusb/tusb.h>

#include "SystemClock.hpp"

#include <chrono>

using namespace modm::platform;
using namespace std::chrono_literals;

// Flash page where application program begins -- this must be aligned to ERASE_PAGES boundary.
// Pages are 512 bytes.
// In this example bootloader, the first 8KB is allocated to the bootloader, the next 8KB
// is allocated to non volatile application data storage, so the application starts
// at 16K, or page 32. If you don't need any free flash, you can adjust PROGRAM_START_PAGE
// to 16. 
// There are some restrictions about what pages can be erased together or separately,
// and these vary depending on the flash sector. You'll need to consult the datasheet
// to make sure any changes you make here work; if you keep sections on 
// 16 page boundaries I think you are OK.
#define PROGRAM_START_PAGE 32
// The number of pages that must be erased at once
#define ERASE_PAGES 16
#define ERASE_BYTES (ERASE_PAGES * 512)
// A GPIO which, when high at reset, will cause the bootloader to remain in DFU mode
// This can be used to force bootloader mode with a button. 
// Can be set to GpioUnused to disable this feature.
using BootPin = GpioA1;
//using BootPin = GpioUnused;

// Just an arbitrary number used as a signal from the application that the
// bootloader should remain in DFU mode
#define BOOTLOADER_FLAG 0x4D49534C

__attribute__( ( naked, noreturn ) ) void jump_asm( uint32_t SP, uint32_t RH )
{
    asm("msr msp, %0\n"
        "bx %1"
        : : "r" (SP), "r" (RH) :
    );
}

void jump_to_application() {
    uint32_t *program_addr = (uint32_t*)(IFLASH_ADDR + PROGRAM_START_PAGE * 512);

    for(uint8_t i=0; i<8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
    }

    // Disable all USB interrupts
    UDP->UDP_IDR = 0xFFFFFFFF;
    // Clear any pending USB interrupts (unclear if this is necessary or not)
    UDP->UDP_ICR = 0xFFFFFFFF;
    // Disable the 48MHz clock
    PMC->PMC_SCDR = PMC_SCER_UDP;
    // Disable the CPU clock to USB
    ClockGen::disable<ClockPeripheral::Udp>();
    // Disable DM/DP function for PA21/22
    MATRIX->CCFG_SYSIO |= (CCFG_SYSIO_SYSIO11 | CCFG_SYSIO_SYSIO10);
    // DISABLE PLL B
    PMC->CKGR_PLLBR = 0;

    // Clear pending IRQ flags
    for(uint8_t i=0; i<8; i++) {
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // Set the vector table offset
    SCB->VTOR = (uint32_t)program_addr;

    // Jump to the app and never returnG
    jump_asm(program_addr[0], program_addr[1]);
}

void set_bootloader_flag(bool flag) {
    if(flag) {
        GPBR->SYS_GPBR[7] = BOOTLOADER_FLAG;
    } else {
        GPBR->SYS_GPBR[7] = 0;
    }
}

bool get_bootloader_flag() {
    return GPBR->SYS_GPBR[7] == BOOTLOADER_FLAG;
}

void restart_watchdog() {
    WDT->WDT_CR = WDT_CR_WDRSTT | WDT_CR_KEY_PASSWD;
}

template<typename Pin>
void setup_boot_pin() {
    // GpioUnused doesn't have a configure method, so this is conditional
    if constexpr (!std::is_base_of<GpioUnused, Pin>()) {
        Pin::configure(Pin::InputType::PullDown);
    }
}

int main() {
    setup_boot_pin<BootPin>();

    // Remain in bootloader if 
    // - the button is pressed, or
    // - if we were reset by the watchdog, or
    // - if the bootloader flag has been set by the application
    if(!BootPin::read() &&
        !get_bootloader_flag() &&
        !((RSTC->RSTC_SR & RSTC_SR_RSTTYP_Msk) == RSTC_SR_RSTTYP_WDT_RST)
    ){
        jump_to_application();
    }

    // Ensure flag is cleared, so that if we reset again we go back to normal boot mode
    set_bootloader_flag(false);

    SystemClock::enable();
    SystemClock::enableUsb();

    // Briefly pull DP low to indicate disconnect to host
    MATRIX->CCFG_SYSIO |= (CCFG_SYSIO_SYSIO11 | CCFG_SYSIO_SYSIO10);
    GpioA22::setOutput(false);
    modm::delay(10ms);
    GpioA22::setInput();

    modm::platform::Usb::initialize<SystemClock>();

    // Initialize USB hardware
    tusb_init();
    while(true) {
        tud_task();
        restart_watchdog();
    }
}

// Buffer to hold a chunk of received data until it is programmed to flash
uint8_t dnbuf[ERASE_BYTES];
uint32_t xfer_offset = 0;

void program_page(uint16_t block_num) {
    // Wait for flash to be not busy
    while(!(EFC->EEFC_FSR & EEFC_FSR_FRDY));

    // Erase 16 512-byte pages
    uint16_t page_number = block_num*16 + PROGRAM_START_PAGE;
    EFC->EEFC_FCR = EEFC_FCR_FCMD_EPA | EEFC_FCR_FARG(page_number | 2) | EEFC_FCR_FKEY_PASSWD;

    // Wait for flash to be not busy
    while(!(EFC->EEFC_FSR & EEFC_FSR_FRDY));

    for(uint32_t page_offset = 0; page_offset < ERASE_PAGES; page_offset++) {
        volatile uint32_t *p = (uint32_t*)(IFLASH_ADDR + (PROGRAM_START_PAGE + page_offset)*512);
        for(uint32_t i=0; i<512; i+=4) {
            p[i/4] = *((uint32_t*)&dnbuf[page_offset * 512 + i]);
        }

        // Write page to flash
        EFC->EEFC_FCR = EEFC_FCR_FCMD_WP | EEFC_FCR_FARG(page_number + page_offset) | EEFC_FCR_FKEY_PASSWD;

        // Wait for flash to be not busy
        while(!(EFC->EEFC_FSR & EEFC_FSR_FRDY));
    }
}

extern "C" {

// Invoked right before tud_dfu_download_cb() (state=DFU_DNBUSY) or tud_dfu_manifest_cb() (state=DFU_MANIFEST)
// Application return timeout in milliseconds (bwPollTimeout) for the next download/manifest operation.
// During this period, USB host won't try to communicate with us.
uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state) {
    (void)alt;
    (void)state;
    return 300; // Datasheet max flash erase time is 200ms
}

// Invoked when received DFU_DNLOAD (wLength>0) following by DFU_GETSTATUS (state=DFU_DNBUSY) requests
// This callback could be returned before flashing op is complete (async).
// Once finished flashing, application must call tud_dfu_finish_flashing()
void tud_dfu_download_cb (uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length) {
    (void)alt;
    (void)block_num;

    uint32_t copied = 0;
    while (copied < length) {
        uint32_t block_offset = xfer_offset % ERASE_BYTES;
        uint32_t copy_size = std::min((uint32_t)length, (uint32_t)(ERASE_BYTES - block_offset));
        memcpy((void*)&dnbuf[block_offset], (void*)&data[copied], copy_size);
        copied += copy_size;
        xfer_offset += copy_size;
        if( (xfer_offset % ERASE_BYTES) == 0 ) {
            program_page(xfer_offset / ERASE_BYTES - 1);
        }
    }

    tud_dfu_finish_flashing(alt);
}

// Invoked when download process is complete, received DFU_DNLOAD (wLength=0) following by DFU_GETSTATUS (state=Manifest)
// Application can do checksum, or actual flashing if buffered entire image previously.
// Once finished flashing, application must call tud_dfu_finish_flashing()
void tud_dfu_manifest_cb(uint8_t alt) {
    (void)alt;

    // Clear any bytes in the buffer that weren't written
    for(uint32_t i = xfer_offset % ERASE_BYTES; i < ERASE_BYTES; i++) {
        dnbuf[i] = 0xFF;
    }
    // Program the last page
    program_page(xfer_offset / ERASE_BYTES);
    // Reset counter in case of second download
    xfer_offset = 0;

    tud_dfu_finish_flashing(alt);
}

// Invoked when received DFU_UPLOAD request
// Application must populate data with up to length bytes and
// Return the number of written bytes
uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length) {
    (void)alt;
    (void)block_num;
    uint32_t read_address = IFLASH_ADDR + PROGRAM_START_PAGE * 512 + xfer_offset;
    uint32_t max_address = IFLASH_ADDR + IFLASH_SIZE;
    uint8_t *p = (uint8_t*)(read_address);
    uint32_t nbytes = 0;
    while(nbytes < length && (uint32_t)p < max_address) {
        data[nbytes++] = *(p++);
        xfer_offset++;
    }

    return nbytes;
}

// Invoked when a DFU_DETACH request is received
void tud_dfu_detach_cb(void) {
    jump_to_application();
}

// Invoked when the Host has terminated a download or upload transfer
void tud_dfu_abort_cb(uint8_t alt) {
    (void)alt;
    xfer_offset = 0;
}
} // extern "C"

// Setup some handlers so that in the event of HardFault or a modm assertion failure,
// we will get trapped here -- rather than resetting -- for easier debugging.
void modm_abandon(const modm::AssertionInfo &info) {
    (void)info;
    while(true) {}
}

extern "C" void HardFault_Handler() {
    while(true) {}
}