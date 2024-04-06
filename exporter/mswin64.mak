# Windows 64-bit version using 
# Microsoft Visual Studio 2017

PROGRAM=domprom

NODEBUG=1

# Link command

n$(PROGRAM).exe: $(PROGRAM).obj
	link /SUBSYSTEM:CONSOLE $(PROGRAM).obj notes0.obj notesai0.obj notes.lib msvcrt.lib user32.lib /PDB:$*.pdb /DEBUG /PDBSTRIPPED:$*_small.pdb -out:$@
	del $*.pdb $*.sym
	rename $*_small.pdb $*.pdb

# Compile command

$(PROGRAM).obj: $(PROGRAM).cpp
	cl -nologo -c -D_MT -MT /Zi /Ot /O2 /Ob2 /Oy- -Gd /Gy /GF /Gs4096 /GS- /favor:INTEL64 /EHsc /Zc:wchar_t- /DWINVER=0x0602 -Zl -W1 -DNT -DW32 -DW -DW64 -DND64 -D_AMD64_ -DDTRACE -D_CRT_SECURE_NO_WARNINGS -DND64SERVER -DPRODUCTION_VERSION /DUSE_WIN32_IDN  $*.cpp

all:
	n$(PROGRAM).exe

clean:
	del *.obj *.pdb *.exe *.dll *.ilk *.sym *.map

