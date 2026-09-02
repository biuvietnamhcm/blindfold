/*
******************************************************************************
**
** @file        : STM32N657X0HXQ_LRUN_debug.ld
**
** @brief       : Debug-only linker script for the BlindFold Appli image.
**                Everything (isr_vector/.text/.rodata/.data/.bss/heap/stack)
**                lives in internal AXI SRAM so a debugger can read/write it
**                at any time -- unlike STM32N657X0HXQ_ROMxspi2.ld, nothing
**                here depends on XSPI2 having been put into memory-mapped
**                mode yet, which is what makes live code-read/breakpoints
**                fail ("not allowed to read") against that build: the debug
**                probe halts the core before FSBL/anything has told the
**                OCTOSPI2 controller to memory-map the flash, so 0x70100400
**                just isn't there yet as far as the probe is concerned.
**                Internal SRAM has no such precondition -- it's addressable
**                from power-up.
**
**                This still assumes FSBL has already run once on real
**                hardware (or is the first step of your debug launch) --
**                Appli itself never calls SystemClock_Config()/enables the
**                caches, so without FSBL you'd be debugging at a wrong
**                clock speed with caches in an undefined state. Load THIS
**                image into RAM and redirect PC to its Reset_Handler as the
**                second stage of that same session, the same way ST's own
**                N6 examples chain FSBL -> RAM-resident Appli for debug.
**
**                Everything below (code, rodata, data, bss, heap, stack)
**                draws from ONE shared internal-SRAM pool capped at
**                0x34000000-0x34100000 (1MB) -- see the matching comment in
**                STM32N657X0HXQ_ROMxspi2.ld and in app_x-cube-ai.c
**                (ai_rgb565_buf): the Neural-ART compiler was told
**                0x34100000-0x343C0000 is its own NPU scratch space, and
**                nothing here is allowed to overlap it. The stock
**                STM32N657X0HXQ_LRUN.ld shipped alongside this file does
**                NOT respect that boundary (its RAM region runs
**                0x34080000-0x34200000, 1MB into the NPU's territory) --
**                that's the one change that matters here, beyond what's
**                explained below. This is why a plain switch to the stock
**                LRUN.ld isn't safe once the AI/NPU path is enabled, even
**                though it'll link without complaint.
**
**                The one thing NOT pulled into that shared pool is the
**                ~2.9MB NPU weights blob (network_atonbuf.xSPI2.c,
**                _mem_pool_xSPI2_network) -- it's routed to NN_ROM
**                (external XSPI2) instead; see the .ai_weights comment
**                below for what that requires. If you're debugging
**                something that doesn't touch the AI pipeline (e.g. the
**                SPI/camera-receive path) and would rather not deal with
**                XSPI2 at all, just don't link network_atonbuf.xSPI2.c /
**                don't call STM32CubeAI_Studio_AI_Init()/_Process() in
**                this build, and delete the .ai_weights section below --
**                EXCLUDE_FILE in .rodata becomes a no-op at that point.
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

_Min_Heap_Size  = 0x200;  /* same as every other variant in this project */
_Min_Stack_Size = 0x2000; /* matches ROMxspi2.ld, not the stock LRUN.ld's
                            * 0x800 -- this app nests SPI5/DMA/EXTI/ETH/JPEG
                            * ISRs on top of a lwIP+AI main loop, so debug
                            * with the same margin the working build uses. */

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);
_sstack = _estack - _Min_Stack_Size;

/* Memories definition
 *
 * RAM spans exactly 0x34000000-0x34100000 (1MB) -- the same boundary
 * ROMxspi2.ld's own RAM region stops at, just carrying .text/.rodata too
 * here since there's no separate external ROM in this configuration.
 * (Measured, not guessed: .bss alone came in at ~642K on the first real
 * build -- ai_rgb565_buf's 200K plus spi_cam_rx.c's/frame_source.c's frame
 * buffers plus lwIP's pool add up fast -- which is exactly why this is one
 * region instead of a pre-split guess: whichever split you pick, something
 * that size will blow past it while the other half sits empty.)
 */
MEMORY
{
  /* One shared pool, not a fixed ROM/RAM split -- there's no real hardware
   * boundary between them here (both are the same internal SRAM), so
   * pre-guessing a split just recreates the "will not fit in region X"
   * error against whichever half runs out first, even though the other
   * half has room to spare. Letting .text/.rodata and .data/.bss draw from
   * the same 1023K pool means the only overflow that can still happen is a
   * real one -- the whole image genuinely exceeds 1MB -- and the linker's
   * error message will say so honestly instead of blaming an arbitrary
   * split point. (1023K, not 1024K, because ORIGIN starts at +0x400.) */
  RAM    (xrw) : ORIGIN = 0x34000400,   LENGTH = 1023K

  /* NPU weights, external XSPI2 -- see the "Sections" comment below before
   * using this. Same origin ROMxspi2.ld already uses for its whole image,
   * so anything already flashed there from a normal ROMxspi2 build lines
   * up if you go with Option A below. */
  NN_ROM (rw)  : ORIGIN = 0x70100400,   LENGTH = 8M
}

/* Sections */
SECTIONS
{
  /* The startup code into "ROM" Rom type memory */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >RAM

  /* The program code and other data into "ROM" Rom type memory */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    *(.RamFunc)
    *(.RamFunc*)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >RAM

  /* Constant data.
   *
   * network_atonbuf.xSPI2.o's ~2.9MB _mem_pool_xSPI2_network array is
   * explicitly excluded here and picked up by the .ai_weights section
   * below instead -- left in this catch-all .rodata, it alone would blow
   * the entire 1MB RAM budget nearly 3x over on its own. If you're not
   * linking that file into this debug build at all (the "skip AI" path
   * from the header comment), EXCLUDE_FILE below is simply a no-op and
   * nothing changes.
   */
  .rodata :
  {
    . = ALIGN(4);
    *(EXCLUDE_FILE(*network_atonbuf.xSPI2.o) .rodata)
    *(EXCLUDE_FILE(*network_atonbuf.xSPI2.o) .rodata*)
    . = ALIGN(4);
  } >RAM

  /* NPU weights blob -- only relevant if network_atonbuf.xSPI2.c is part of
   * this build. Two ways to keep the AI path working while everything else
   * debugs from RAM:
   *
   *   Option A (reuse flash you've already programmed): mark this NOLOAD so
   *   the linker assigns _mem_pool_xSPI2_network its address here but never
   *   asks the debugger to write the 2.9MB of data -- it just reads
   *   whatever XSPI2 already has from your last full ROMxspi2 flash. Only
   *   correct if that flash's *last* build placed this exact object at
   *   *this* offset from ROM's start -- check your ROMxspi2.ld .map file
   *   for `_mem_pool_xSPI2_network`'s address; if it's not 0x70100400,
   *   replace the origin above (or add a PROVIDE() override here) to match
   *   before relying on this.
   *
   *   Option B (deterministic, no guessing): build network_atonbuf.xSPI2.c
   *   once as its own tiny standalone image -- entry point irrelevant, just
   *   this one array linked at a fixed origin (e.g. this same 0x70100400) -
   *   -flash that once via CubeProgrammer, then in this LRUN build drop the
   *   file entirely and add `PROVIDE(_mem_pool_xSPI2_network = 0x70100400);`
   *   near the top of this script instead of the section below, so nothing
   *   here depends on incidental layout from an unrelated build.
   *
   * Uncomment whichever you use; both leave .text/.rodata/.data/.bss fully
   * in internal RAM, so live debug (breakpoints, stepping, disassembly)
   * still never touches XSPI2.
   */
  .ai_weights (NOLOAD) :
  {
    . = ALIGN(64);
    *network_atonbuf.xSPI2.o(.rodata .rodata*)
  } > NN_ROM

  .ARM.extab :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >RAM

  .ARM :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >RAM

  .preinit_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >RAM

  .init_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >RAM

  .fini_array :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >RAM

  /* Used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections into "RAM" Ram type memory */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)

    . = ALIGN(4);
    _edata = .;
  } >RAM

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;
  } > RAM

  /* ETH1 DMA descriptor rings -- explicit home, same as every other
   * variant in this project (see the matching comment in
   * STM32N657X0HXQ_ROMxspi2.ld). NOLOAD: HAL_ETH_Init() fills every field
   * itself. */
  .eth_descriptors (NOLOAD) :
  {
    . = ALIGN(32);
    KEEP(*(.RxDecripSection))
    KEEP(*(.TxDecripSection))
    . = ALIGN(32);
  } > RAM

  .gnu.sgstubs :
  {
    _sNSCVeneer = .;
    . = ALIGN(4);
    *(.gnu.sgstubs*)
    . = ALIGN(4);
  } >RAM
  _eNSCVeneer = .;

  /* Uninitialized data section into "RAM" Ram type memory */
  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
