/* TODO: complete the script */

ENTRY(_hw3_dance)

PHDRS
{
	text PT_LOAD FLAGS PF_W PF_X ;
	data PT_LOAD FLAGS PF_W ;
	rodata PT_LOAD FLAGS PF_R PF_X ;
}

SECTIONS
{
  . = 0x20000;
  .data : 
    {
	    *(.data)
    } :data
  .bss :
    {
	    *(.bss)
    } :data

  //TODO: add other sections
  . = 0x400000;
  .text: 
  {
    *(.text)
  } :text

  . = 0x90000000;
  .rodata : 
  {
	*(.rodata)
  } :rodata

  .symtab : 
  {
    *(.symtab)
    greens (Ndx=UND)
    blacks (Ndx=UND)
  }

}
