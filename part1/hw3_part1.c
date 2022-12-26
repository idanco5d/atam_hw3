#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reg.h>
#include <sys/user.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include "elf64.h"

#define ET_NONE 0 // No file type
#define ET_REL 1  // Relocatable file
#define ET_EXEC 2 // Executable file
#define ET_DYN 3  // Shared object file
#define ET_CORE 4 // Core file
#define SHF_EXECINSTR 0x4

/* symbol_name		- The symbol (maybe function) we need to search for.
 * exe_file_name	- The file where we search the symbol in.
 * error_val		- If  1: A global symbol was found, and defined in the given executable.
 * 			- If -1: Symbol not found.
 *			- If -2: Only a local symbol was found.
 * 			- If -3: File is not an executable.
 * 			- If -4: The symbol was found, it is global, but it is not defined in the executable.
 * return value		- The address which the symbol_name will be loaded to, if the symbol was found and is global.
 */

// TODO: psodo
/*
unsigned long find_symbol(char *symbol_name, char *exe_file_name, int *error_val){
	elf header <- beginning of file
	if elf_header.type != executable or header is null:
		error not executable, return;
	all_sections_array = elf_header + sh_offset ("All sections array now contains all sections")
	all_sections_array[elf_header.section_string_table] -> section_string_table ("This table contains the strings of all sections")
	Find string table ("The table which contains the strings of all symbols"):
		Iterate over all sections array:
			For each section, find the offset name
			section name = section_string_table + offset name
			if section name == strtab then this the section, return it
	Find symbol table: the same as above, just compare to symtab
	Iterate over symbol table entries:
		For each symbol, find the symbol name offset
		actual name = string_table + offset
		if actual_name == *symbol_name:
			Check st_info field of the symbol
			if st_info is global, return the symbol offset
*/
#define SHT_STRTAB 3
#define SHT_SYMTAB 2
#define PT_LOAD 1
#define STB_GLOBAL 1

int getIndex(Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table, char *section_name);
Elf64_Shdr *getSectionHeader(int fd, Elf64_Ehdr *elf_header);
char *getSymbolTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table);
char *getStringTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table);
char *getSectionHeaderStringTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header);
Elf64_Phdr *getProgramHeader(int fd, Elf64_Ehdr *elf_header);
Elf64_Sym *getSymbol(Elf64_Shdr *symbol_table_header, char *string_table, char *symbol_table, char *symbol_name);

void freeAllAndClose(int fd, char *section_header_string_table, char *symbol_table, char *string_table, Elf64_Phdr *program_header);

unsigned long find_symbol(char *symbol_name, char *exe_file_name, int *error_val)
{
	if (!symbol_name || !exe_file_name)
	{
		*error_val = -1;
		return 0;
	}
	// printf("symbol_name: %s, exe_file_name: %s\n", symbol_name, exe_file_name);
	int fd = open(exe_file_name, O_RDONLY);
	if (fd < 0)
	{
		*error_val = -3;
		return 0;
	}
	Elf64_Ehdr elf_header;
	read(fd, &elf_header, sizeof(elf_header));
	if (elf_header.e_type != ET_EXEC)
	{
		close(fd);
		*error_val = -3;
		return 0;
	}

	Elf64_Shdr *section_header = getSectionHeader(fd, &elf_header);

	char *section_header_string_table = getSectionHeaderStringTable(fd, section_header, &elf_header);

	char *symbol_table = getSymbolTable(fd, section_header, &elf_header, section_header_string_table);

	char *string_table = getStringTable(fd, section_header, &elf_header, section_header_string_table);

	int symtab_index = getIndex(section_header, &elf_header, section_header_string_table, ".symtab");
	Elf64_Shdr symbol_table_header = section_header[symtab_index];
	int num_of_symbols = symbol_table_header.sh_size / symbol_table_header.sh_entsize;
	Elf64_Sym *symbol = getSymbol(&symbol_table_header, string_table, symbol_table, symbol_name);
	if (!symbol)
	{
		*error_val = -1;
		freeAllAndClose(fd, section_header_string_table, symbol_table, string_table, NULL);
		return 0;
	}
	if (symbol->st_shndx != STB_GLOBAL)
	{
		*error_val = -2;
		freeAllAndClose(fd, section_header_string_table, symbol_table, string_table, NULL);
		return 0;
	}
	Elf64_Phdr *program_header = getProgramHeader(fd, &elf_header);
	for (int i = 0; i < elf_header.e_phnum; i++)
	{
		if (program_header[i].p_flags == SHF_EXECINSTR)
		{
			if (symbol->st_value >= program_header[i].p_vaddr && symbol->st_value < program_header[i].p_vaddr + program_header[i].p_memsz)
			{
				*error_val = 1;
				freeAllAndClose(fd, section_header_string_table, symbol_table, string_table, program_header);
				return symbol->st_value;
			}
		}
	}
	*error_val = -4;
	freeAllAndClose(fd, section_header_string_table, symbol_table, string_table, program_header);
	return 0;
}

Elf64_Shdr *getSectionHeader(int fd, Elf64_Ehdr *elf_header)
{
	Elf64_Shdr *section_header = malloc(elf_header->e_shentsize * elf_header->e_shnum);
	lseek(fd, elf_header->e_shoff, SEEK_SET);
	read(fd, section_header, elf_header->e_shentsize * elf_header->e_shnum);
	return section_header;
}

int getIndex(Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table, char *section_name)
{
	for (int i = 0; i < elf_header->e_shnum; i++)
	{
		if (strcmp(section_header_string_table + section_header[i].sh_name, section_name) == 0)
		{
			return i;
		}
	}
	return -1;
}
char *getSymbolTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table)
{
	int symtab_index = getIndex(section_header, elf_header, section_header_string_table, ".symtab");
	Elf64_Shdr symbol_table_header = section_header[symtab_index];
	char *symbol_table = (char *)malloc(symbol_table_header.sh_size);
	lseek(fd, symbol_table_header.sh_offset, SEEK_SET);
	read(fd, symbol_table, symbol_table_header.sh_size);
	return symbol_table;
}

char *getStringTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header, char *section_header_string_table)
{
	int symtab_index = getIndex(section_header, elf_header, section_header_string_table, ".strtab");
	Elf64_Shdr symbol_table_header = section_header[symtab_index];
	char *symbol_table = (char *)malloc(symbol_table_header.sh_size);
	lseek(fd, symbol_table_header.sh_offset, SEEK_SET);
	read(fd, symbol_table, symbol_table_header.sh_size);
	return symbol_table;
}

char *getSectionHeaderStringTable(int fd, Elf64_Shdr *section_header, Elf64_Ehdr *elf_header)
{
	Elf64_Shdr header_section_header_string_table = section_header[elf_header->e_shstrndx];
	char *section_header_string_table = (char *)malloc(header_section_header_string_table.sh_size);
	lseek(fd, header_section_header_string_table.sh_offset, SEEK_SET);
	read(fd, section_header_string_table, header_section_header_string_table.sh_size);
	return section_header_string_table;
}

Elf64_Sym *getSymbol(Elf64_Shdr *symbol_table_header, char *string_table, char *symbol_table, char *symbol_name)
{
	int num_of_symbols = symbol_table_header->sh_size / symbol_table_header->sh_entsize;
	Elf64_Sym *ret_symbol = NULL;
	for (int i = 0; i < num_of_symbols; ++i)
	{
		Elf64_Sym *symbol = (Elf64_Sym *)(symbol_table + i * symbol_table_header->sh_entsize);
		char *symbol_name = string_table + symbol->st_name;
		if (strcmp(symbol_name, symbol_name) == 0)
		{
			if (symbol->st_info == STB_GLOBAL)
			{
				return symbol;
			}
			ret_symbol = symbol;
		}
	}
	return ret_symbol;
}

Elf64_Phdr *getProgramHeader(int fd, Elf64_Ehdr *elf_header)
{
	Elf64_Phdr *program_header = malloc(elf_header->e_phentsize * elf_header->e_phnum);
	lseek(fd, elf_header->e_phoff, SEEK_SET);
	read(fd, program_header, elf_header->e_phentsize * elf_header->e_phnum);
	return program_header;
}

void freeAllAndClose(int fd, char *section_header_string_table, char *symbol_table, char *string_table, Elf64_Phdr *program_header)
{
	free(section_header_string_table);
	free(symbol_table);
	free(string_table);
	free(program_header);
	close(fd);
}

int main(int argc, char *const argv[])
{
	//! to remove!
	char **args = malloc(sizeof(char *) * 3);
	args[0] = malloc(sizeof(char) * 100);
	args[1] = malloc(sizeof(char) * 100);
	args[2] = malloc(sizeof(char) * 100);
	strcpy(args[1], "hash");
	strcpy(args[2], "verySecretProgram");

	//!
	int err = 0;
	//! change to argv
	unsigned long addr = find_symbol(args[1], args[2], &err);

	if (addr > 0)
		printf("%s will be loaded to 0x%lx\n", args[1], addr);
	else if (err == -2)
		printf("%s is not a global symbol! :(\n", args[1]);
	else if (err == -1)
		printf("%s not found!\n", args[1]);
	else if (err == -3)
		printf("%s not an executable! :(\n", args[2]);
	else if (err == -4)
		printf("%s is a global symbol, but will come from a shared library\n", args[1]);

	//! to remove!
	free(args[0]);
	free(args[1]);
	free(args[2]);
	free(args);
	//!
	return 0;
}