/* Default linker script, for normal executables */
OUTPUT_FORMAT("elf32-pru","elf32-pru","elf32-pru")
OUTPUT_ARCH(pru)
/* Allow memory sizes to be overridden from command line.  */
__IMEM_SIZE = DEFINED(__IMEM_SIZE) ? __IMEM_SIZE : 8K;
__DMEM_SIZE = DEFINED(__DMEM_SIZE) ? __DMEM_SIZE : 8K;
MEMORY
{
  imem   (x)   : ORIGIN = 0x20000000, LENGTH = __IMEM_SIZE
  dmem   (rw!x) : ORIGIN = 0x0, LENGTH = __DMEM_SIZE
}
__HEAP_SIZE = DEFINED(__HEAP_SIZE) ? __HEAP_SIZE : 32;
__STACK_SIZE = DEFINED(__STACK_SIZE) ? __STACK_SIZE : 512;
PROVIDE (_stack_top = ORIGIN(dmem) + LENGTH(dmem));
ENTRY (_start)
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  .hash          : { *(.hash)		}
  .dynsym        : { *(.dynsym)		}
  .dynstr        : { *(.dynstr)		}
  .gnu.version   : { *(.gnu.version)	}
  .gnu.version_d   : { *(.gnu.version_d)	}
  .gnu.version_r   : { *(.gnu.version_r)	}
  .rel.init      : { *(.rel.init)		}
  .rela.init     : { *(.rela.init)	}
  .rel.text      :
    {
      *(.rel.text)
      *(.rel.text.*)
      *(.rel.text:*)
      *(.rel.gnu.linkonce.t*)
    }
  .rela.text     :
    {
      *(.rela.text)
      *(.rela.text.*)
      *(.rela.text:*)
      *(.rela.gnu.linkonce.t*)
    }
  .rel.fini      : { *(.rel.fini)		}
  .rela.fini     : { *(.rela.fini)	}
  .rel.rodata    :
    {
      *(.rel.rodata)
      *(.rel.rodata.*)
      *(.rel.rodata:*)
      *(.rel.gnu.linkonce.r*)
    }
  .rela.rodata   :
    {
      *(.rela.rodata)
      *(.rela.rodata.*)
      *(.rela.rodata:*)
      *(.rela.gnu.linkonce.r*)
    }
  .rel.data      :
    {
      *(.rel.data)
      *(.rel.data.*)
      *(.rel.data:*)
      *(.rel.gnu.linkonce.d*)
    }
  .rela.data     :
    {
      *(.rela.data)
      *(.rela.data.*)
      *(.rela.data:*)
      *(.rela.gnu.linkonce.d*)
    }
  .rel.init_array   	  : { *(.rel.init_array)	}
  .rela.init_array  	  : { *(.rela.init_array)	}
  .rel.fini_array   	  : { *(.rel.fini_array)	}
  .rela.fini_array  	  : { *(.rela.fini_array)	}
  .rel.got     		  : { *(.rel.got)		}
  .rela.got    		  : { *(.rela.got)	}
  .rel.bss     		  : { *(.rel.bss)		}
  .rela.bss    		  : { *(.rela.bss)	}
  .rel.plt     		  : { *(.rel.plt)		}
  .rela.plt    		  : { *(.rela.plt)	}
  /* Internal text space.  */
  .text   :
  {
     _text_start = . ;
    . = ALIGN(4);
    *(.init0)  /* Start here after reset.  */
    KEEP (*(.init0))
    . = ALIGN(4);
    *(.text)
    . = ALIGN(4);
    *(.text.*)
    . = ALIGN(4);
    *(.text:*)
    . = ALIGN(4);
    *(.gnu.linkonce.t*)
    . = ALIGN(4);
     _text_end = . ;
  }  > imem
  .data          :
  {
    /* Optional variable that user is prepared to have NULL address.  */
     *(.data.atzero*)
    /* CRT is prepared for constructor/destructor table to have
       a "valid" NULL address.  */
     __init_array_start = . ;
     KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*)))
     KEEP (*(.init_array))
     __init_array_end = . ;
     __fini_array_start = . ;
     KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
     KEEP (*(.fini_array))
     __fini_array_end = . ;
    /* DATA memory starts at address 0.  So to avoid placing a valid static
       variable at the invalid NULL address, we introduce the .data.atzero
       section.  If CRT can make some use of it - great.  Otherwise skip a
       word.  In all cases .data/.bss sections must start at non-zero.  */
    . += (. == 0 ? 4 : 0);
     PROVIDE (_data_start = .) ;
    *(.data)
     *(.data*)
     *(.data:*)
     *(.rodata)  /* We need to include .rodata here if gcc is used.  */
     *(.rodata.*) /* with -fdata-sections.  */
     *(.rodata:*)
    *(.gnu.linkonce.d*)
    *(.gnu.linkonce.r*)
    . = ALIGN(4);
     PROVIDE (_data_end = .) ;
  }  > dmem
  /* Linux remoteproc loader requires the resource_table section
     start address to be aligned to 8 bytes.  */
  .resource_table    ALIGN(8) :
  {
    KEEP (*(.resource_table))
  }  > dmem
  .bss   :
  {
     PROVIDE (_bss_start = .) ;
    *(.bss)
     *(.bss.*)
     *(.bss:*)
    *(.gnu.linkonce.b*)
    *(COMMON)
     PROVIDE (_bss_end = .) ;
  }  > dmem
  /* Global data not cleared after reset.  */
  .noinit   :
  {
     PROVIDE (_noinit_start = .) ;
    *(.noinit .noinit.* .gnu.linkonce.n.*)
     PROVIDE (_noinit_end = .) ;
     PROVIDE (_heap_start = .) ;
     . += __HEAP_SIZE ;
    /* Stack is not here really.  It will be put at the end of DMEM.
       But we take into account its size here, in order to allow
       for MEMORY overflow checking during link time.  */
     . += __STACK_SIZE ;
  }  > dmem
  /* Remoteproc loader in Linux kernel 5.10 and later reads this section
     to setup the PRUSS interrupt controller.  The interrupt map section
     is never referenced from PRU firmware, so there is no need to
     place it in the target dmem memory.  */
  .pru_irq_map 0 : { *(.pru_irq_map) }
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  .note.gnu.build-id   : { *(.note.gnu.build-id) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1.  */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions.  */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2.  */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2.  */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions.  */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* DWARF 3.  */
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  /* DWARF 5.  */
  .debug_addr     0 : { *(.debug_addr) }
  .debug_line_str 0 : { *(.debug_line_str) }
  .debug_loclists 0 : { *(.debug_loclists) }
  .debug_macro    0 : { *(.debug_macro) }
  .debug_names    0 : { *(.debug_names) }
  .debug_rnglists 0 : { *(.debug_rnglists) }
  .debug_str_offsets 0 : { *(.debug_str_offsets) }
  .debug_sup      0 : { *(.debug_sup) }
}
