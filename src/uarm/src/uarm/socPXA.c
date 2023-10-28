//(c) uARM project    https://github.com/uARM-Palm/uARM    uARM@dmitry.gr

#include "CPU.h"
#include "MMU.h"
#include "RAM.h"
#include "ROM.h"
#include "SoC.h"
#include "cp15.h"
#include "mem.h"
#include "soc_AC97.h"
#include "soc_DMA.h"
#include "soc_GPIO.h"
#include "soc_I2C.h"
#include "soc_I2S.h"
#include "soc_IC.h"
#include "soc_SSP.h"
#include "soc_UART.h"

// all PXAs
#include "pxa_DMA.h"
#include "pxa_LCD.h"
#include "pxa_MMC.h"
#include "pxa_MemCtrl.h"
#include "pxa_PWM.h"
#include "pxa_PwrClk.h"
#include "pxa_RTC.h"
#include "pxa_SSP.h"
#include "pxa_TIMR.h"
#include "pxa_UART.h"

// PXA25x/PXA26x
#include "pxa255_DSP.h"
#include "pxa255_UDC.h"

// PXA27x
#include <stdlib.h>
#include <string.h>

#include "device.h"
#include "keys.h"
#include "pxa270_IMC.h"
#include "pxa270_KPC.h"
#include "pxa270_UDC.h"
#include "pxa270_WMMX.h"
#include "util.h"
#include "vSD.h"

#define CPUID_PXA255 0x69052D06ul  // spepping A0
#define CPUID_PXA260 0x69052D06ul  // spepping B1
#define CPUID_PXA270 0x49265013ul  // stepping C0

#define SRAM_BASE 0x5c000000ul
#define SRAM_SIZE 0x00040000ul

#define ROM_BASE 0x00000000UL
#define RAM_BASE 0xA0000000UL

struct SoC {
    struct SocUart *ffUart, *hwUart, *stUart, *btUart;
    struct SocSsp *ssp[3];
    struct SocGpio *gpio;
    struct SocAC97 *ac97;
    struct SocDma *dma;
    struct SocI2s *i2s;
    struct SocI2c *i2c;
    struct SocIc *ic;
    bool mouseDown;

    struct PxaMemCtrlr *memCtrl;
    struct PxaPwrClk *pwrClk;
    struct PxaPwm *pwm[4];
    struct PxaTimr *tmr;
    struct PxaMmc *mmc;
    struct PxaRtc *rtc;
    struct PxaLcd *lcd;

    union {
        // 25x/26x
        struct {
            struct Pxa255dsp *dsp;
            struct Pxa255Udc *udc1;
        };
        // PXA27x
        struct {
            struct Pxa270wmmx *wmmx;
            struct Pxa270Udc *udc2;
            struct PxaImc *imc;
            struct PxaKpc *kpc;
        };
    };

    struct ArmRam *sram;
    struct ArmRam *ram;
    struct ArmRam *ramMirror;  // mirror
    struct ArmRom *rom;
    struct ArmMem *mem;
    struct ArmCpu *cpu;
    struct Keypad *kp;
    struct VSD *vSD;

    struct Device *dev;

    uint64_t accumulated_cycles;
};

static uint_fast16_t socUartPrvRead(void *userData) {
    uint_fast16_t v;
    int r;

    r = socExtSerialReadChar();

    if (r == CHAR_CTL_C)
        v = UART_CHAR_BREAK;
    else if (r == CHAR_NONE)
        v = UART_CHAR_NONE;
    else if (r >= 0x100)
        v = UART_CHAR_NONE;  // we canot send this char!!!
    else
        v = r;

    return v;
}

static void socUartPrvWrite(uint_fast16_t chr, void *userData) {
    if (chr == UART_CHAR_NONE) return;

    socExtSerialWriteChar(chr);
}

struct SoC *socInit(void **romPieces, const uint32_t *romPieceSizes, uint32_t romNumPieces,
                    uint32_t sdNumSectors, SdSectorR sdR, SdSectorW sdW, FILE *nandFile,
                    int gdbPort, uint_fast8_t socRev) {
    struct SoC *soc = (struct SoC *)malloc(sizeof(struct SoC));
    struct SocPeriphs sp = {};
    uint32_t *ramBuffer;

    memset(soc, 0, sizeof(*soc));

    soc->mem = memInit();
    if (!soc->mem) ERR("Cannot init physical memory manager");

    soc->cpu = cpuInit(ROM_BASE, soc->mem, true /* xscale */, false /* omap */, gdbPort,
                       socRev ? ((socRev == 1) ? CPUID_PXA260 : CPUID_PXA270) : CPUID_PXA255,
                       0x0B16A16AUL);
    if (!soc->cpu) ERR("Cannot init CPU");

    ramBuffer = (uint32_t *)malloc(deviceGetRamSize());
    if (!ramBuffer) ERR("cannot alloc RAM space\n");

    soc->ram = ramInit(soc->mem, RAM_BASE, deviceGetRamSize(), ramBuffer);
    if (!soc->ram) ERR("Cannot init RAM");

    // ram mirror for rom probe code
    soc->ramMirror =
        ramInit(soc->mem, RAM_BASE + deviceGetRamSize(), deviceGetRamSize(), ramBuffer);
    if (!soc->ramMirror) ERR("Cannot init RAM mirror");

    soc->rom =
        romInit(soc->mem, ROM_BASE, romPieces, romPieceSizes, romNumPieces, deviceGetRomMemType());
    if (!soc->rom) ERR("Cannot init ROM1");

    soc->ic = socIcInit(soc->cpu, soc->mem, socRev);
    if (!soc->ic) ERR("Cannot init PXA's IC");

    soc->dma = socDmaInit(soc->mem, soc->ic);
    if (!soc->dma) ERR("Cannot init PXA's DMA");

    if (socRev == 0 || socRev == 1) {
        soc->dsp = pxa255dspInit(soc->cpu);
        if (!soc->dsp) ERR("Cannot init PXA255's DSP");
    } else if (socRev == 2) {
        soc->wmmx = pxa270wmmxInit(soc->cpu);
        if (!soc->wmmx) ERR("Cannot init PXA270's WMMX");

        soc->imc = pxaImcInit(soc->mem);
        if (!soc->imc) ERR("Cannot init PXA270's IMC");

        soc->kpc = pxaKpcInit(soc->mem, soc->ic);
        if (!soc->kpc) ERR("Cannot init PXA270's KPC");

        // SRAM
        ramBuffer = (uint32_t *)malloc(SRAM_SIZE);
        if (!ramBuffer) ERR("cannot alloc SRAM space\n");

        soc->sram = ramInit(soc->mem, SRAM_BASE, SRAM_SIZE, ramBuffer);
        if (!soc->ram) ERR("Cannot init SRAM");
    }

    soc->gpio = socGpioInit(soc->mem, soc->ic, socRev);
    if (!soc->gpio) ERR("Cannot init PXA's GPIO");

    soc->tmr = pxaTimrInit(soc->mem, soc->ic);
    if (!soc->tmr) ERR("Cannot init PXA's OSTIMER");

    soc->rtc = pxaRtcInit(soc->mem, soc->ic);
    if (!soc->rtc) ERR("Cannot init PXA's RTC");

    soc->ffUart = socUartInit(soc->mem, soc->ic, PXA_FFUART_BASE, PXA_I_FFUART);
    if (!soc->ffUart) ERR("Cannot init PXA's FFUART");

    if (socRev != 2) {
        soc->hwUart = socUartInit(soc->mem, soc->ic, PXA_HWUART_BASE, PXA_I_HWUART);
        if (!soc->hwUart) ERR("Cannot init PXA's HWUART");
    }

    soc->stUart = socUartInit(soc->mem, soc->ic, PXA_STUART_BASE, PXA_I_STUART);
    if (!soc->stUart) ERR("Cannot init PXA's STUART");

    soc->btUart = socUartInit(soc->mem, soc->ic, PXA_BTUART_BASE, PXA_I_BTUART);
    if (!soc->btUart) ERR("Cannot init PXA's BTUART");

    soc->pwrClk = pxaPwrClkInit(soc->cpu, soc->mem);
    if (!soc->pwrClk) ERR("Cannot init PXA's PWRCLKMGR");

    soc->i2c = socI2cInit(soc->mem, soc->ic, soc->dma);
    if (!soc->i2c) ERR("Cannot init PXA's I2C");

    soc->memCtrl = pxaMemCtrlrInit(soc->mem, socRev);
    if (!soc->memCtrl) ERR("Cannot init PXA's MEMC");

    soc->ac97 = socAC97Init(soc->mem, soc->ic, soc->dma);
    if (!soc->ac97) ERR("Cannot init PXA's AC97");

    // SSP/SSP1
    soc->ssp[0] = socSspInit(soc->mem, soc->ic, soc->dma, PXA_SSP1_BASE, PXA_I_SSP, DMA_CMR_SSP_RX);
    if (!soc->ssp[0]) ERR("Cannot init PXA's SSP1");

    if (socRev == 0 || socRev == 1) {
        // NSSP
        soc->ssp[1] =
            socSspInit(soc->mem, soc->ic, soc->dma, PXA_NSSP_BASE, PXA_I_NSSP, DMA_CMR_NSSP_RX);
        if (!soc->ssp[1]) ERR("Cannot init PXA's NSSP");

        soc->udc1 = pxa255UdcInit(soc->mem, soc->ic, soc->dma);
        if (!soc->udc1) ERR("Cannot init PXA255's UDC");
    }

    if (socRev == 1) {
        // ASSP
        soc->ssp[2] =
            socSspInit(soc->mem, soc->ic, soc->dma, PXA_ASSP_BASE, PXA_I_ASSP, DMA_CMR_ASSP_RX);
        if (!soc->ssp[2]) ERR("Cannot init PXA26x's ASSP");
    }

    if (socRev == 2) {
        // SSP2
        soc->ssp[1] =
            socSspInit(soc->mem, soc->ic, soc->dma, PXA_SSP2_BASE, PXA_I_SSP2, DMA_CMR_SSP2_RX);
        if (!soc->ssp[1]) ERR("Cannot init PXA27x's SSP2");

        // SSP3
        soc->ssp[2] =
            socSspInit(soc->mem, soc->ic, soc->dma, PXA_SSP3_BASE, PXA_I_SSP3, DMA_CMR_SSP3_RX);
        if (!soc->ssp[2]) ERR("Cannot init PXA27x's SSP3");

        soc->udc2 = pxa270UdcInit(soc->mem, soc->ic, soc->dma);
        if (!soc->udc2) ERR("Cannot init PXA270's UDC");
    }

    soc->i2s = socI2sInit(soc->mem, soc->ic, soc->dma);
    if (!soc->i2s) ERR("Cannot init PXA's I2S");

    soc->pwm[0] = pxaPwmInit(soc->mem, PXA_PWM0_BASE);
    if (!soc->pwm[0]) ERR("Cannot init PXA's PWM0");

    soc->pwm[1] = pxaPwmInit(soc->mem, PXA_PWM1_BASE);
    if (!soc->pwm[1]) ERR("Cannot init PXA's PWM1");

    if (socRev == 2) {
        soc->pwm[2] = pxaPwmInit(soc->mem, PXA_PWM2_BASE);
        if (!soc->pwm[2]) ERR("Cannot init PXA's PWM2");

        soc->pwm[3] = pxaPwmInit(soc->mem, PXA_PWM3_BASE);
        if (!soc->pwm[3]) ERR("Cannot init PXA's PWM3");
    }

    soc->mmc = pxaMmcInit(soc->mem, soc->ic, soc->dma);
    if (!soc->mmc) ERR("Cannot init PXA's MMC");

    struct DeviceDisplayConfiguration displayConfiguration;
    deviceGetDisplayConfiguration(&displayConfiguration);

    soc->lcd =
        pxaLcdInit(soc->mem, soc->ic, displayConfiguration.width, displayConfiguration.height);
    if (!soc->lcd) ERR("Cannot init PXA's LCD");

    soc->kp = keypadInit(soc->gpio, true);
    if (!soc->kp) ERR("Cannot init keypad controller");

    if (sdNumSectors) {
        soc->vSD = vsdInit(sdR, sdW, sdNumSectors);
        if (!soc->vSD) ERR("Cannot init vSD");

        pxaMmcInsert(soc->mmc, soc->vSD);
    }

    sp.mem = soc->mem;
    sp.gpio = soc->gpio;
    sp.i2c = soc->i2c;
    sp.i2s = soc->i2s;
    sp.ac97 = soc->ac97;
    sp.ssp = soc->ssp[0];
    sp.ssp2 = soc->ssp[1];
    sp.ssp3 = soc->ssp[2];
    if (socRev == 2) sp.kpc = soc->kpc;

    soc->dev = deviceSetup(&sp, soc->kp, soc->vSD, nandFile);
    if (!soc->dev) ERR("Cannot init device\n");

    if (sp.dbgUart) socUartSetFuncs(sp.dbgUart, socUartPrvRead, socUartPrvWrite, soc->hwUart);

    /*
            var gpio = {latches: [0x30000, 0x1400001, 0x200], inputs: [0x786c06, 0x100, 0x0],
       levels: [0x7b6c04, 0x1400101, 0x200], dirs: [0xcf878178, 0xffd1beff, 0x1ffff], riseDet:
       [0x1680, 0x40000, 0x0], fallDet: [0x786c04, 0x0, 0x0], AFRs: [0x4, 0xa500000a, 0x60008010,
       0xaaa00000, 0xaa0000a, 0x0]};


            var i;

            document.write("<table
       border=1><tr><td>GPIO</td><td>DIR</td><td>VAL</td><td>AFR</td><td>EDGE</td><td>NOTES</td></tr>");

            for (i = 0; i <= 80; i++) {

                    document.write("<tr><td>" + i + "</td>");

              if (gpio.dirs[i >> 5] & (1 << (i & 31))) {
                    document.write("<td>OUT</td>");
                    document.write("<td>" + ((gpio.latches[i >> 5] & (1 << (i & 31))) ? "HI" : "LO")
       + "</td>");
              }
              else {
                    document.write("<td>IN</td><td></td>");
              }

              document.write("<td>" + ((gpio.AFRs[i >> 4] >> ((i & 15) * 2)) & 3) + "</td>");
              document.write("<td>");

              if (gpio.riseDet[i >> 5] & (1 << (i & 31)))
                    document.write("RE ");
              if (gpio.fallDet[i >> 5] & (1 << (i & 31)))
                    document.write("FE ");


              document.write("</td>");

              document.write("<td></td></tr>");
            }


            document.write("</table>");
    */

    return soc;
}

void socKeyDown(struct SoC *soc, enum KeyId key) {
    deviceKey(soc->dev, key, true);
    keypadKeyEvt(soc->kp, key, true);
}

void socKeyUp(struct SoC *soc, enum KeyId key) {
    deviceKey(soc->dev, key, false);
    keypadKeyEvt(soc->kp, key, false);
}

void socPenDown(struct SoC *soc, int x, int y) {
    soc->mouseDown = true;
    deviceTouch(soc->dev, x, y);
}

void socPenUp(struct SoC *soc) {
    soc->mouseDown = false;
    deviceTouch(soc->dev, -1, -1);
}

uint64_t socRun(struct SoC *soc, uint64_t maxCycles) {
    uint64_t cycles = 0;
    uint_fast8_t i;

    while (cycles < maxCycles) {
        cycles++;
        soc->accumulated_cycles++;

        if (!(soc->accumulated_cycles & 0x00000007UL)) pxaTimrTick(soc->tmr);
        if (!(soc->accumulated_cycles & 0x000000FFUL)) {
            for (i = 0; i < 3; i++) {
                if (soc->ssp[i]) socSspPeriodic(soc->ssp[i]);
            }
        }
        if (!(soc->accumulated_cycles & 0x000000FFUL)) socDmaPeriodic(soc->dma);
        if (!(soc->accumulated_cycles & 0x000007FFUL)) socAC97Periodic(soc->ac97);
        if (!(soc->accumulated_cycles & 0x000007FFUL)) socI2sPeriodic(soc->i2s);
        if (!(soc->accumulated_cycles & 0x000000FFUL)) {
            socUartProcess(soc->ffUart);
            if (soc->hwUart) socUartProcess(soc->hwUart);
            socUartProcess(soc->stUart);
            socUartProcess(soc->btUart);
        }

        devicePeriodic(soc->dev, cycles);

        if (!(soc->accumulated_cycles & 0x0001FFFUL)) pxaLcdFrame(soc->lcd);
        if (!(soc->accumulated_cycles & 0x00FFFFFFUL)) pxaRtcUpdate(soc->rtc);

        cpuCycle(soc->cpu);
    }

    return cycles;
}

uint32_t *socGetPendingFrame(struct SoC *soc) { return pxaLcdGetPendingFrame(soc->lcd); }

void socResetPendingFrame(struct SoC *soc) { return pxaLcdResetPendingFrame(soc->lcd); }