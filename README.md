# rnc / dernc - fixed version for rnc-tools from dungeon master utilies

This is my branch of the rnc-tool with fixes applied so that compressed files can be used in amiga as well.

Original license still applies (MIT)

---

RNC/DeRNC packer for Windows with source (gcc)

From Dungeon Keeper utilities, rebuilt & fixed by JOTD & Temisu (http://eab.abime.net/showthread.php?p=1161592)

How to use:

pack:  rnc file1 file2 ...
unpack: dernc file1 file2 ...

the packing is safe. unpack is done internally and file is not overwritten with the packed version if something goes wrong.

The packed files were tested with commercial RN unpacker and XFDDecrunch and they work, even if the packing ratio may be not as good as the original packer.
